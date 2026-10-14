/* Copyright 2026 Martin Lonkwitz (btop-aix)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/

// Platform layer for IBM AIX (6.1 TL7 and later).
//
// Data sources (all unprivileged, all available since AIX 6.1):
//   CPU      perfstat_cpu_total() / perfstat_cpu()    ticks user/sys/idle/wait, load average, clock
//   Memory   perfstat_memory_total()                  real/free/file cache/paging space (4 KiB pages)
//   Disks    mntctl(MCTL_QUERY) + statvfs()           mounted file systems and their capacity
//            perfstat_disk()                          physical volumes (hdiskN) with I/O counters
//            perfstat_logicalvolume()                 per LV I/O, only if LVM statistics are enabled
//   Network  perfstat_netinterface()                  byte counters; getifaddrs() replacement for addresses
//   Procs    getprocs64() + getargs()                 process table, command lines
//
// Structure sizes passed to libperfstat start with the ones of the build headers;
// an older library that rejects them is handled in namespace AixPerfstat below.

#include <libperfstat.h>
#include <procinfo.h>
#include <sys/corral.h>
#include <sys/mntctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/vmount.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <numeric>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "../btop_config.hpp"
#include "../btop_log.hpp"
#include "../btop_shared.hpp"
#include "../btop_tools.hpp"
#include "aix_ifaddrs.hpp"

using std::clamp, std::string_literals::operator""s, std::cmp_equal, std::cmp_less, std::cmp_greater;
using std::numeric_limits, std::round, std::max, std::min;
namespace fs = std::filesystem;
namespace rng = std::ranges;
using namespace Tools;

//? --------------------------------------------------- AIX constants ---------------------------------------------------

namespace AixProc {
	//? Process states of procentry64::pi_state; <procinfo.h> pulls in <sys/proc.h>, whose
	//? SIDL/SZOMB/... are macros, hence the distinct names here (values identical)
	constexpr unsigned STATE_IDL = 4;     // process is created
	constexpr unsigned STATE_ZOMB = 5;    // process is dying
	constexpr unsigned STATE_STOP = 6;    // process is stopped
	constexpr unsigned STATE_ACTIVE = 7;  // process is active
	constexpr unsigned STATE_SWAP = 8;    // process is swapped
	//? pi_flags bit: kernel process (SKPROC)
	constexpr unsigned FLAG_KPROC = 0x00000200;
	//? Scale of perfstat load averages: (1 << SBITS), SBITS from <sys/proc.h>
	constexpr int LOAD_SBITS = 16;
	//? perfstat memory counters are in 4 KiB pages regardless of the page size in use
	constexpr uint64_t PERFSTAT_PAGE = 4096;
	//? AIX nice values are 0..39 with 20 as the default; btop shows the Unix range -20..19
	constexpr int NICE_OFFSET = 20;
}

//? --------------------------------------------------- FUNCTIONS -----------------------------------------------------

namespace AixPerfstat {
	//? libperfstat compares sizeof_struct with the structure size the installed library knows and rejects
	//? a larger value with EINVAL: built against the 6100-09 headers perfstat_cpu_t is 624 bytes, the
	//? 6100-07 library accepts 616 and perfstat_netinterface_t 224 instead of 240 (7100-02: cpu 616 for a
	//? 7100-05 build with 640; measured with tools/aix/probes/perfstat_size_probe.c, the CPU box stayed
	//? empty). Sizes do not only grow (netinterface_t: 240 in 6.1.8, 224 in 7.1.0/7.1.1). Every field btop
	//? reads ends within the smallest structure size (last byte read: cpu_t 96, netinterface_t 184, disk_t 264,
	//? logicalvolume_t 216, cpu_total_t 224, memory_total_t 336 for real_avail, which exists since 6.1 TL7;
	//? table tools/aix/probes/perfstat_layout/offsets_5.3-7.3.txt), fields beyond it read as zero. The accepted size is probed once per call; array entries are copied out of a raw
	//? buffer with that stride, single structures are read the same way.
	//? stride/probed are atomic: Tools::system_uptime() reads cpu_total from the main thread while the
	//? runner thread collects; two threads probing at the same time store the same result.
	template <typename T>
	class Array {
	public:
		using Fn = int (*)(perfstat_id_t *, T *, int, int);  // sizeof_struct is int in libperfstat.h

		explicit Array(const char *what, const char *first_name) : what(what), first_name(first_name) {}

		//* Number of available entries (the count query), < 0 on error with errno set
		int count(Fn fn) {
			if (not probe(fn)) return -1;
			return fn(nullptr, nullptr, (int)stride.load(std::memory_order_relaxed), 0);
		}

		//* Up to `wanted` entries from the first one into `out`; returns the number delivered, < 0 on error
		int fetch(Fn fn, int wanted, vector<T> &out) {
			out.clear();
			if (wanted <= 0 or not probe(fn)) return wanted <= 0 ? 0 : -1;
			const size_t size = stride.load(std::memory_order_relaxed);  // <= sizeof(T), so raw is large enough
			vector<char> raw((size_t)wanted * sizeof(T));
			perfstat_id_t first{};
			std::strncpy(first.name, first_name, sizeof first.name - 1);
			const int got = fn(&first, reinterpret_cast<T *>(raw.data()), (int)size, wanted);
			if (got < 0) return got;
			out.resize((size_t)got);
			for (int i = 0; i < got; i++) {
				out[(size_t)i] = T{};
				std::memcpy(&out[(size_t)i], raw.data() + (size_t)i * size, size);
			}
			return got;
		}

		[[nodiscard]] size_t accepted_size() const { return stride.load(std::memory_order_relaxed); }

	private:
		//* Until the first success: shrink the structure size in steps of 8 bytes until the library accepts it
		bool probe(Fn fn) {
			if (probed.load(std::memory_order_acquire)) return true;
			size_t size = sizeof(T);
			T one{};
			for (;;) {
				perfstat_id_t first{};
				std::strncpy(first.name, first_name, sizeof first.name - 1);
				errno = 0;
				if (fn(&first, &one, (int)size, 1) >= 0) break;
				//? a failure for another reason (EACCES, statistics switched off) is not a size problem: no
				//? shrinking, the caller sees the error; the next refresh tries again (one call), so statistics
				//? switched on later are still read with the size the library accepts
				if (errno != EINVAL or size <= sizeof(T) / 2) return false;
				size -= 8;
			}
			stride.store(size, std::memory_order_relaxed);
			probed.store(true, std::memory_order_release);
			if (size != sizeof(T))
				Logger::info("{}: libperfstat accepts {} of {} bytes per entry (older release than the build headers)", what, size, sizeof(T));
			return true;
		}

		const char *what;
		const char *first_name;
		std::atomic<size_t> stride{sizeof(T)};
		std::atomic<bool> probed{false};
	};

	//? Same for the "total" calls (perfstat_cpu_total, perfstat_memory_total): no name, one structure.
	template <typename T>
	class Total {
	public:
		using Fn = int (*)(perfstat_id_t *, T *, int, int);
		explicit Total(const char *what) : what(what) {}

		//* Reads the structure into `out` (zero padded beyond the accepted size); false on error with errno set
		bool get(Fn fn, T &out) {
			const bool done = probed.load(std::memory_order_acquire);
			size_t size = stride.load(std::memory_order_relaxed);
			for (;;) {
				out = T{};
				errno = 0;
				if (fn(nullptr, &out, (int)size, 1) == 1) break;
				if (done or errno != EINVAL or size <= sizeof(T) / 2) return false;
				size -= 8;
			}
			if (not done) {
				stride.store(size, std::memory_order_relaxed);
				probed.store(true, std::memory_order_release);
				if (size != sizeof(T))
					Logger::info("{}: libperfstat accepts {} of {} bytes (older release than the build headers)", what, size, sizeof(T));
			}
			return true;
		}

	private:
		const char *what;
		std::atomic<size_t> stride{sizeof(T)};
		std::atomic<bool> probed{false};
	};

	inline bool cpu_total(perfstat_cpu_total_t &ct) {
		static Total<perfstat_cpu_total_t> t("perfstat_cpu_total");
		return t.get(perfstat_cpu_total, ct);
	}
	inline bool memory_total(perfstat_memory_total_t &mt) {
		static Total<perfstat_memory_total_t> t("perfstat_memory_total");
		return t.get(perfstat_memory_total, mt);
	}
}  // namespace AixPerfstat

namespace Cpu {
	vector<long long> core_old_totals;
	vector<long long> core_old_idles;
	vector<string> available_fields = {"Auto", "total"};
	vector<string> available_sensors = {"Auto"};
	cpu_info current_cpu;
	bool got_sensors = false, cpu_temp_only = false, supports_watts = false;

	//* AIX exposes no CPU temperature through perfstat; keep the sensor list empty
	bool get_sensors();

	//* Clock speed from perfstat_cpu_total_t::processorHZ (nominal, not the current frequency)
	string get_cpuHz();

	//* Processor description from perfstat (e.g. "PowerPC_POWER10")
	string get_cpuName();

	string cpu_sensor;
	vector<string> core_sensors;
	std::unordered_map<int, int> core_mapping;

	//? Last global tick counters for Proc::collect()
	uint64_t total_ticks = 0;
}  // namespace Cpu

namespace Mem {
	double old_uptime;
}

namespace Shared {
	fs::path passwd_path;
	uint64_t totalMem;
	long page_size, clk_tck, coreCount, physicalCoreCount, arg_max;
	int totalMem_len;
	long bootTime;

	//? Resize per-core containers after a DLPAR operation changed the number of logical CPUs
	void set_core_count(long count) {
		coreCount = max(1l, count);
		Cpu::current_cpu.core_percent.resize(coreCount);
		Cpu::current_cpu.temp.resize(coreCount + 1);
		Cpu::core_old_totals.resize(coreCount, 0);
		Cpu::core_old_idles.resize(coreCount, 0);
	}

	void init() {
		//? Shared global variables init
		perfstat_cpu_total_t ct{};
		long ncpus = 0;
		if (AixPerfstat::cpu_total(ct)) {
			ncpus = ct.ncpus;
		} else {
			Logger::warning("perfstat_cpu_total() failed: {}", strerror(errno));
			ncpus = sysconf(_SC_NPROCESSORS_ONLN);
		}
		if (ncpus <= 0) {
			ncpus = 1;
			Logger::warning("Could not determine number of cores, defaulting to 1.");
		}
		coreCount = ncpus;
		physicalCoreCount = ncpus;

		page_size = sysconf(_SC_PAGESIZE);
		if (page_size <= 0) {
			page_size = 4096;
			Logger::warning("Could not get system page size. Defaulting to 4096, processes memory usage might be incorrect.");
		}

		clk_tck = sysconf(_SC_CLK_TCK);
		if (clk_tck <= 0) {
			clk_tck = 100;
			Logger::warning("Could not get system clock ticks per second. Defaulting to 100, processes cpu usage might be incorrect.");
		}

		perfstat_memory_total_t mt{};
		if (AixPerfstat::memory_total(mt)) {
			totalMem = mt.real_total * AixProc::PERFSTAT_PAGE;
		} else {
			Logger::warning("perfstat_memory_total() failed: {}", strerror(errno));
			totalMem = 0;
		}

		//? lbolt = ticks since boot
		bootTime = time(nullptr) - (ct.lbolt > 0 ? ct.lbolt / clk_tck : 0);

		//* Get maximum length of process arguments
		arg_max = sysconf(_SC_ARG_MAX);

		//? Init for namespace Cpu
		Cpu::current_cpu.core_percent.insert(Cpu::current_cpu.core_percent.begin(), Shared::coreCount, {});
		Cpu::current_cpu.temp.insert(Cpu::current_cpu.temp.begin(), Shared::coreCount + 1, {});
		Cpu::core_old_totals.insert(Cpu::core_old_totals.begin(), Shared::coreCount, 0);
		Cpu::core_old_idles.insert(Cpu::core_old_idles.begin(), Shared::coreCount, 0);
		Logger::debug("Init -> Cpu::collect()");
		Cpu::collect();
		for (auto &[field, vec] : Cpu::current_cpu.cpu_percent) {
			if (not vec.empty() and not v_contains(Cpu::available_fields, field)) Cpu::available_fields.push_back(field);
		}
		Logger::debug("Init -> Cpu::get_cpuName()");
		Cpu::cpuName = Cpu::get_cpuName();
		Logger::debug("Init -> Cpu::get_sensors()");
		Cpu::got_sensors = Cpu::get_sensors();
		Logger::debug("Init -> Cpu::get_core_mapping()");
		Cpu::core_mapping = Cpu::get_core_mapping();

		//? WPAR detection: a non-zero corral id means btop runs inside a workload partition
		if (wpar_getcid() != 0) Cpu::container_engine = "WPAR";

		//? Init for namespace Mem
		Mem::old_uptime = system_uptime();
		Logger::debug("Init -> Mem::collect()");
		Mem::collect();
	}
}  // namespace Shared

namespace Cpu {
	string cpuName;
	string cpuHz;
	bool has_battery = false;
	tuple<int, float, long, string> current_bat;

	//? perfstat names -> btop field names ("nice" does not exist on AIX)
	const array<string, 4> time_names = {"user", "system", "idle", "iowait"};

	std::unordered_map<string, long long> cpu_old = {
		{"totals", 0},
		{"idles", 0},
		{"user", 0},
		{"system", 0},
		{"idle", 0},
		{"iowait", 0}
	};

	string get_cpuName() {
		perfstat_cpu_total_t ct{};
		if (not AixPerfstat::cpu_total(ct)) {
			Logger::error("Failed to get CPU name");
			return "";
		}
		string name(ct.description, strnlen(ct.description, sizeof ct.description));
		rng::replace(name, '_', ' ');
		return trim_name(name);
	}

	bool get_sensors() {
		got_sensors = false;
		return got_sensors;
	}

	string get_cpuHz() {
		perfstat_cpu_total_t ct{};
		if (not AixPerfstat::cpu_total(ct) or ct.processorHZ == 0) return "";
		const double mhz = (double)ct.processorHZ / 1'000'000.0;
		if (mhz < 1000.0) return fmt::format("{} MHz", (int)round(mhz));
		return fmt::format("{:.{}f} GHz", mhz / 1000.0, mhz / 1000.0 < 10 ? 2 : 1);
	}

	auto get_core_mapping() -> std::unordered_map<int, int> {
		std::unordered_map<int, int> core_map;
		if (cpu_temp_only) return core_map;

		for (long i = 0; i < Shared::coreCount; i++) {
			core_map[i] = i;
		}

		//? Apply user set custom mapping if any
		const auto &custom_map = Config::getS("cpu_core_map");
		if (not custom_map.empty()) {
			try {
				for (const auto &split : ssplit(custom_map)) {
					const auto vals = ssplit(split, ':');
					if (vals.size() != 2) continue;
					int change_id = std::stoi(vals.at(0));
					int new_id = std::stoi(vals.at(1));
					if (not core_map.contains(change_id) or cmp_greater(new_id, core_sensors.size())) continue;
					core_map.at(change_id) = new_id;
				}
			} catch (...) {
			}
		}

		return core_map;
	}

	auto get_battery() -> tuple<int, float, long, string> {
		return {0, 0, 0, ""};
	}

	auto collect(bool no_update) -> cpu_info & {
		if (Runner::stopping or (no_update and not current_cpu.cpu_percent.at("total").empty()))
			return current_cpu;
		auto &cpu = current_cpu;

		perfstat_cpu_total_t ct{};
		if (not AixPerfstat::cpu_total(ct)) {
			Logger::error("perfstat_cpu_total() failed: {}", strerror(errno));
			return cpu;
		}

		for (size_t i = 0; i < cpu.load_avg.size(); i++)
			cpu.load_avg[i] = (double)ct.loadavg[i] / (double)(1 << AixProc::LOAD_SBITS);

		//? Number of logical CPUs can change at runtime (DLPAR)
		if (ct.ncpus > 0 and ct.ncpus != Shared::coreCount) {
			Logger::info("Number of logical CPUs changed from {} to {}", Shared::coreCount, ct.ncpus);
			Shared::set_core_count(ct.ncpus);
			Runner::coreNum_reset = true;
		}

		//? Per logical CPU
		static AixPerfstat::Array<perfstat_cpu_t> cpu_array("perfstat_cpu", FIRST_CPU);
		vector<perfstat_cpu_t> cpus;
		const int got = cpu_array.fetch(perfstat_cpu, (int)Shared::coreCount, cpus);
		if (got < 0) {
			Logger::error("perfstat_cpu() failed: {}", strerror(errno));
		}
		for (int i = 0; i < got and i < Shared::coreCount; i++) {
			const auto &c = cpus[i];
			const long long totals = (long long)(c.user + c.sys + c.idle + c.wait);
			const long long idles = (long long)(c.idle + c.wait);

			const long long calc_totals = max(0ll, totals - core_old_totals.at(i));
			const long long calc_idles = max(0ll, idles - core_old_idles.at(i));
			core_old_totals.at(i) = totals;
			core_old_idles.at(i) = idles;

			cpu.core_percent.at(i).push_back(calc_totals > 0 ? clamp((long long)round((double)(calc_totals - calc_idles) * 100 / calc_totals), 0ll, 100ll) : 0ll);

			//? Reduce size if there are more values than needed for graph
			if (cpu.core_percent.at(i).size() > 40) cpu.core_percent.at(i).pop_front();
		}

		//? Totals over all logical CPUs
		const array<long long, 4> times = {(long long)ct.user, (long long)ct.sys, (long long)ct.idle, (long long)ct.wait};
		const long long global_totals = std::accumulate(times.begin(), times.end(), 0ll);
		const long long global_idles = times[2] + times[3];
		total_ticks = (uint64_t)global_totals;

		const long long calc_totals = max(1ll, global_totals - cpu_old.at("totals"));
		const long long calc_idles = max(1ll, global_idles - cpu_old.at("idles"));

		//? Populate cpu.cpu_percent with all fields from perfstat
		for (int ii = 0; const auto &val : times) {
			const auto &name = time_names.at(ii);
			cpu.cpu_percent.at(name).push_back(clamp((long long)round((double)(val - cpu_old.at(name)) * 100 / calc_totals), 0ll, 100ll));
			cpu_old.at(name) = val;

			//? Reduce size if there are more values than needed for graph
			while (cmp_greater(cpu.cpu_percent.at(name).size(), width * 2)) cpu.cpu_percent.at(name).pop_front();

			ii++;
		}

		cpu_old.at("totals") = global_totals;
		cpu_old.at("idles") = global_idles;

		//? Total usage of cpu
		cpu.cpu_percent.at("total").push_back(clamp((long long)round((double)(calc_totals - calc_idles) * 100 / calc_totals), 0ll, 100ll));

		//? Reduce size if there are more values than needed for graph
		while (cmp_greater(cpu.cpu_percent.at("total").size(), width * 2)) cpu.cpu_percent.at("total").pop_front();

		if (Config::getB("show_cpu_freq")) {
			//? "hz" is a macro from <sys/m_param.h> on AIX, hence the name
			auto clock_str = get_cpuHz();
			if (not clock_str.empty()) {
				cpuHz = clock_str;
			}
		}

		return cpu;
	}
}  // namespace Cpu

namespace Mem {
	bool has_swap = false;
	int disk_ios = 0;
	vector<string> last_found;

	mem_info current_mem{};

	//? Previous "time disk is active" (ms) per hdisk for the activity percentage
	std::unordered_map<string, uint64_t> old_disk_time;

	uint64_t get_totalMem() {
		return Shared::totalMem;
	}

	//? Names of mntctl() file system types from <sys/vmount.h>
	string fstype_name(int gfstype) {
		switch (gfstype) {
			case MNT_J2: return "jfs2";
			case MNT_JFS: return "jfs";
			case MNT_NFS: return "nfs";
			case MNT_NFS3: return "nfs3";
			case MNT_NFS4: return "nfs4";
			case MNT_CDROM: return "cdrfs";
			case MNT_UDF: return "udfs";
			case MNT_VXFS: return "vxfs";
			case MNT_CACHEFS: return "cachefs";
			case MNT_NAMEFS: return "namefs";
			case MNT_PROCFS: return "procfs";
			case MNT_AUTOFS: return "autofs";
			case MNT_SFS: return "sfs";
			default: return fmt::format("vfs{}", gfstype);
		}
	}

	//? Pseudo file systems that carry no storage
	bool is_pseudo_fs(int gfstype) {
		switch (gfstype) {
			case MNT_NAMEFS: case MNT_PROCFS: case MNT_SFS: case MNT_AUTOFS:
			case MNT_POOLFS: case MNT_AHAFS: case MNT_PMEMFS: case MNT_RFS4:
				return true;
			default:
				return false;
		}
	}

	struct MountEntry {
		string mountpoint;
		string device;
		string fstype;
		bool remote = false;
	};

	//* Mounted file systems via mntctl(MCTL_QUERY); O(n) in the number of mounts
	vector<MountEntry> get_mounts() {
		vector<MountEntry> result;
		int size = 0;
		if (mntctl(MCTL_QUERY, sizeof size, reinterpret_cast<char*>(&size)) != 0 or size <= 0) {
			Logger::warning("mntctl(MCTL_QUERY) size query failed: {}", strerror(errno));
			return result;
		}
		vector<char> buf(static_cast<size_t>(size));
		const int count = mntctl(MCTL_QUERY, size, buf.data());
		if (count <= 0) {
			Logger::warning("mntctl(MCTL_QUERY) failed: {}", strerror(errno));
			return result;
		}
		const char* p = buf.data();
		for (int i = 0; i < count; i++) {
			const auto* vm = reinterpret_cast<const struct vmount*>(p);
			MountEntry e;
			e.mountpoint = vmt2dataptr(vm, VMT_STUB);
			e.device = vmt2dataptr(vm, VMT_OBJECT);
			e.fstype = fstype_name(vm->vmt_gfstype);
			e.remote = (vm->vmt_flags & MNT_REMOTE) != 0;
			if (e.remote) {
				const char* host = vmt2dataptr(vm, VMT_HOSTNAME);
				if (host != nullptr and *host != '\0') e.device = string(host) + ":" + e.device;
			}
			if (not is_pseudo_fs(vm->vmt_gfstype) and not e.mountpoint.empty())
				result.push_back(std::move(e));
			p += vm->vmt_length;
		}
		return result;
	}

	void push_io(disk_info& disk, int64_t readBytes, int64_t writeBytes, int64_t activity) {
		if (disk.io_read.empty()) {
			disk.io_read.push_back(0);
		} else {
			disk.io_read.push_back(max((int64_t)0, (readBytes - disk.old_io.at(0))));
		}
		disk.old_io.at(0) = readBytes;
		while (cmp_greater(disk.io_read.size(), width * 2)) disk.io_read.pop_front();

		if (disk.io_write.empty()) {
			disk.io_write.push_back(0);
		} else {
			disk.io_write.push_back(max((int64_t)0, (writeBytes - disk.old_io.at(1))));
		}
		disk.old_io.at(1) = writeBytes;
		while (cmp_greater(disk.io_write.size(), width * 2)) disk.io_write.pop_front();

		disk.io_activity.push_back(clamp(activity, (int64_t)0, (int64_t)100));
		while (cmp_greater(disk.io_activity.size(), width * 2)) disk.io_activity.pop_front();
	}

	//* I/O per logical volume; delivers data only when LVM statistics are enabled (lvmstat -e), otherwise nothing
	void collect_lv_io(std::unordered_map<string, disk_info>& disks, const std::unordered_map<string, string>& lv_to_mount) {
		static bool lv_stats_available = true;
		if (not lv_stats_available or lv_to_mount.empty()) return;

		static AixPerfstat::Array<perfstat_logicalvolume_t> lv_array("perfstat_logicalvolume", FIRST_LOGICALVOLUME);
		const int count = lv_array.count(perfstat_logicalvolume);
		if (count <= 0) {
			lv_stats_available = false;
			Logger::info("perfstat_logicalvolume() unavailable (LVM statistics not enabled); per file system I/O is not shown");
			return;
		}
		vector<perfstat_logicalvolume_t> lvs;
		const int got = lv_array.fetch(perfstat_logicalvolume, count, lvs);
		for (int i = 0; i < got; i++) {
			const string lvname(lvs[i].name, strnlen(lvs[i].name, sizeof lvs[i].name));
			auto it = lv_to_mount.find(lvname);
			if (it == lv_to_mount.end() or not disks.contains(it->second)) continue;
			auto& disk = disks.at(it->second);
			disk_ios++;
			const int64_t rd = (int64_t)lvs[i].kbreads * 1024;
			const int64_t wr = (int64_t)lvs[i].kbwrites * 1024;
			const int64_t activity = (disk.io_read.empty() ? 0 : clamp((int64_t)round((double)((rd - disk.old_io.at(0)) + (wr - disk.old_io.at(1))) / (1 << 20)), (int64_t)0, (int64_t)100));
			push_io(disk, rd, wr, activity);
		}
	}

	//* Physical volumes (hdiskN) from perfstat_disk(): capacity = allocated/free physical partitions, I/O counters, busy time
	void collect_hdisks(mem_info& mem, const vector<string>& filter, bool filter_exclude, double uptime, vector<string>& found) {
		static AixPerfstat::Array<perfstat_disk_t> disk_array("perfstat_disk", FIRST_DISK);
		const int count = disk_array.count(perfstat_disk);
		if (count <= 0) return;
		vector<perfstat_disk_t> pds;
		const int got = disk_array.fetch(perfstat_disk, count, pds);
		if (got < 0) {
			Logger::warning("perfstat_disk() failed: {}", strerror(errno));
			return;
		}
		auto& disks = mem.disks;
		for (int i = 0; i < got; i++) {
			const auto& d = pds[i];
			const string name(d.name, strnlen(d.name, sizeof d.name));
			if (name.empty()) continue;

			//? Match filter if not empty
			if (not filter.empty()) {
				bool match = v_contains(filter, name);
				if ((filter_exclude and match) or (not filter_exclude and not match))
					continue;
			}

			found.push_back(name);
			if (not disks.contains(name)) {
				disks[name] = disk_info{fs::path("/dev/" + name), name};
				disks.at(name).fstype = string(d.vgname, strnlen(d.vgname, sizeof d.vgname));
				redraw = true;
			}
			auto& disk = disks.at(name);
			disk.total = (int64_t)d.size << 20;
			disk.free = (int64_t)d.free << 20;
			disk.used = disk.total - disk.free;
			if (disk.total != 0) {
				disk.used_percent = round((double)disk.used * 100 / disk.total);
				disk.free_percent = 100 - disk.used_percent;
			} else {
				disk.used_percent = 0;
				disk.free_percent = 0;
			}

			disk_ios++;
			const int64_t rd = (int64_t)(d.rblks * d.bsize);
			const int64_t wr = (int64_t)(d.wblks * d.bsize);
			//? d.time = accumulated milliseconds the disk was busy
			int64_t activity = 0;
			if (auto it = old_disk_time.find(name); it != old_disk_time.end() and uptime > old_uptime) {
				activity = (int64_t)round((double)(d.time - it->second) * 100 / ((uptime - old_uptime) * 1000));
			}
			old_disk_time[name] = d.time;
			push_io(disk, rd, wr, activity);
		}
	}

	auto collect(bool no_update) -> mem_info & {
		if (Runner::stopping or (no_update and not current_mem.percent.at("used").empty()))
			return current_mem;

		auto show_swap = Config::getB("show_swap");
		auto show_disks = Config::getB("show_disks");
		auto swap_disk = Config::getB("swap_disk");
		auto only_physical = Config::getB("only_physical");
		auto &mem = current_mem;
		static bool snapped = (getenv("BTOP_SNAPPED") != nullptr);

		perfstat_memory_total_t mt{};
		if (not AixPerfstat::memory_total(mt)) {
			Logger::error("perfstat_memory_total() failed: {}", strerror(errno));
			return mem;
		}
		const uint64_t page = AixProc::PERFSTAT_PAGE;
		//? numperm = frames holding file pages (the AIX file cache); real_inuse includes them
		const uint64_t cached = mt.numperm * page;
		const uint64_t inuse = mt.real_inuse * page;
		mem.stats.at("free") = mt.real_free * page;
		mem.stats.at("cached") = cached;
		mem.stats.at("used") = inuse > cached ? inuse - cached : inuse;
		//? real_avail = memory available without paging out working segments (free + reclaimable file pages)
		mem.stats.at("available") = mt.real_avail > 0 ? mt.real_avail * page : mem.stats.at("free") + cached;

		if (show_swap) {
			mem.stats.at("swap_total") = mt.pgsp_total * page;
			mem.stats.at("swap_free") = mt.pgsp_free * page;
			mem.stats.at("swap_used") = mem.stats.at("swap_total") - mem.stats.at("swap_free");
		}

		if (show_swap and mem.stats.at("swap_total") > 0) {
			for (const auto &name : swap_names) {
				mem.percent.at(name).push_back(round((double)mem.stats.at(name) * 100 / mem.stats.at("swap_total")));
				while (cmp_greater(mem.percent.at(name).size(), width * 2))
					mem.percent.at(name).pop_front();
			}
			has_swap = true;
		} else
			has_swap = false;
		//? Calculate percentages
		for (const auto &name : mem_names) {
			mem.percent.at(name).push_back(Shared::totalMem > 0 ? round((double)mem.stats.at(name) * 100 / Shared::totalMem) : 0);
			while (cmp_greater(mem.percent.at(name).size(), width * 2))
				mem.percent.at(name).pop_front();
		}

		if (show_disks) {
			double uptime = system_uptime();
			auto &disks_filter = Config::getS("disks_filter");
			bool filter_exclude = false;
			auto &disks = mem.disks;
			vector<string> filter;
			if (not disks_filter.empty()) {
				filter = ssplit(disks_filter);
				if (filter.at(0).starts_with("exclude=")) {
					filter_exclude = true;
					filter.at(0) = filter.at(0).substr(8);
				}
			}

			vector<string> found;
			found.reserve(last_found.size());
			std::unordered_map<string, string> lv_to_mount;

			for (const auto& m : get_mounts()) {
				//? only_physical hides remote file systems (NFS automounts can run into the hundreds on SAP hosts)
				if (only_physical and m.remote) continue;

				//? Match filter if not empty
				if (not filter.empty()) {
					bool match = v_contains(filter, m.mountpoint);
					if ((filter_exclude and match) or (not filter_exclude and not match))
						continue;
				}

				found.push_back(m.mountpoint);
				if (not disks.contains(m.mountpoint)) {
					disks[m.mountpoint] = disk_info{fs::path(m.device), fs::path(m.mountpoint).filename()};
					if (disks.at(m.mountpoint).name.empty())
						disks.at(m.mountpoint).name = (m.mountpoint == "/" ? "root" : m.mountpoint);
					disks.at(m.mountpoint).fstype = m.fstype;
				}
				if (not m.remote and m.device.starts_with("/dev/"))
					lv_to_mount[m.device.substr(5)] = m.mountpoint;

				if (not v_contains(last_found, m.mountpoint))
					redraw = true;
			}

			//? Get disk/partition stats
			for (const auto& mountpoint : found) {
				auto& disk = disks.at(mountpoint);
				struct statvfs vfs;
				if (statvfs(mountpoint.c_str(), &vfs) < 0) {
					Logger::warning("Failed to get disk/partition stats with statvfs() for: {}", mountpoint);
					continue;
				}
				disk.total = (int64_t)vfs.f_blocks * (int64_t)vfs.f_frsize;
				disk.free = (int64_t)vfs.f_bfree * (int64_t)vfs.f_frsize;
				disk.used = disk.total - disk.free;
				if (disk.total != 0) {
					disk.used_percent = round((double)disk.used * 100 / disk.total);
					disk.free_percent = 100 - disk.used_percent;
				} else {
					disk.used_percent = 0;
					disk.free_percent = 0;
				}
			}

			disk_ios = 0;
			//? Per file system I/O (needs LVM statistics), then the physical volumes
			collect_lv_io(disks, lv_to_mount);
			for (const auto& mountpoint : found) {
				auto& disk = disks.at(mountpoint);
				if (disk.io_read.empty() or disk.io_read.size() < disks.at(found.front()).io_read.size())
					push_io(disk, disk.old_io.at(0), disk.old_io.at(1), 0);
			}
			const size_t n_mounts = found.size();
			collect_hdisks(mem, filter, filter_exclude, uptime, found);

			//? Remove disks no longer mounted or filtered out
			if (swap_disk and has_swap) found.push_back("swap");
			for (auto it = disks.begin(); it != disks.end();) {
				if (not v_contains(found, it->first))
					it = disks.erase(it);
				else
					it++;
			}
			if (found.size() != last_found.size()) redraw = true;
			last_found = std::move(found);

			//? Setup disks order in UI and add swap if enabled
			mem.disks_order.clear();
			if (snapped and disks.contains("/mnt"))
				mem.disks_order.push_back("/mnt");
			else if (disks.contains("/"))
				mem.disks_order.push_back("/");
			if (swap_disk and has_swap) {
				mem.disks_order.push_back("swap");
				if (not disks.contains("swap"))
					disks["swap"] = {"", "swap"};
				disks.at("swap").total = mem.stats.at("swap_total");
				disks.at("swap").used = mem.stats.at("swap_used");
				disks.at("swap").free = mem.stats.at("swap_free");
				disks.at("swap").used_percent = mem.percent.at("swap_used").back();
				disks.at("swap").free_percent = mem.percent.at("swap_free").back();
			}
			for (size_t i = 0; i < last_found.size(); i++) {
				const auto& name = last_found[i];
				if (i >= n_mounts and name == "swap") continue;
				if (not is_in(name, "/", "swap", "/dev"))
					mem.disks_order.push_back(name);
			}

			old_uptime = uptime;
		}
		return mem;
	}

}  // namespace Mem

namespace Net {
	std::unordered_map<string, net_info> current_net;
	net_info empty_net = {};
	vector<string> interfaces;
	string selected_iface;
	int errors = 0;
	std::unordered_map<string, uint64_t> graph_max = {{"download", {}}, {"upload", {}}};
	std::unordered_map<string, array<int, 2>> max_count = {{"download", {}}, {"upload", {}}};
	bool rescale = true;
	uint64_t timestamp = 0;

	auto collect(bool no_update) -> net_info & {
		auto &net = current_net;
		auto &config_iface = Config::getS("net_iface");
		auto net_sync = Config::getB("net_sync");
		auto net_auto = Config::getB("net_auto");
		auto new_timestamp = time_ms();

		if (not no_update and errors < 3) {
			//? Byte counters and the authoritative interface list from perfstat
			std::unordered_map<string, std::tuple<uint64_t, uint64_t>> ifstats;
			static AixPerfstat::Array<perfstat_netinterface_t> net_array("perfstat_netinterface", FIRST_NETINTERFACE);
			const int count = net_array.count(perfstat_netinterface);
			if (count < 0) {
				errors++;
				Logger::error("Net::collect() -> perfstat_netinterface() failed: {}", strerror(errno));
				redraw = true;
				return empty_net;
			}
			vector<perfstat_netinterface_t> nis;
			const int got = net_array.fetch(perfstat_netinterface, count, nis);
			interfaces.clear();
			for (int i = 0; i < got; i++) {
				const string name(nis[i].name, strnlen(nis[i].name, sizeof nis[i].name));
				if (name.empty()) continue;
				interfaces.push_back(name);
				ifstats[name] = std::tuple(nis[i].ibytes, nis[i].obytes);
				net[name].connected = false;
				net[name].ipv4.clear();
				net[name].ipv6.clear();
			}

			//? Addresses and flags using the getifaddrs() replacement (SIOCGIFCONF)
			IfAddrsPtr if_addrs {};
			if (if_addrs.get_status() != 0) {
				errors++;
				Logger::error("Net::collect() -> getifaddrs() failed with id {}", if_addrs.get_status());
				redraw = true;
				return empty_net;
			}
			int family = 0;
			static_assert(INET6_ADDRSTRLEN >= INET_ADDRSTRLEN); // 46 >= 16, compile-time assurance.
			enum { IPBUFFER_MAXSIZE = INET6_ADDRSTRLEN }; // manually using the known biggest value, guarded by the above static_assert
			char ip[IPBUFFER_MAXSIZE];

			//? Iteration over all items in getifaddrs() list
			for (auto *ifa = if_addrs.get(); ifa != nullptr; ifa = ifa->ifa_next) {
				if (ifa->ifa_addr == nullptr) continue;
				family = ifa->ifa_addr->sa_family;
				const string iface = ifa->ifa_name;
				//? Interfaces without perfstat counters (rare) are still listed
				if (not v_contains(interfaces, iface)) {
					interfaces.push_back(iface);
					net[iface].ipv4.clear();
					net[iface].ipv6.clear();
				}
				net[iface].connected = (ifa->ifa_flags & IFF_RUNNING);

				//? Get IPv4 address
				if (family == AF_INET) {
					if (net[iface].ipv4.empty()) {
						if (nullptr != inet_ntop(family, &(reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr)->sin_addr), ip, IPBUFFER_MAXSIZE)) {
							net[iface].ipv4 = ip;
						} else {
							int errsv = errno;
							Logger::error("Net::collect() -> Failed to convert IPv4 to string for iface {}, errno: {}", iface, strerror(errsv));
						}
					}
				}
				//? Get IPv6 address
				else if (family == AF_INET6) {
					if (net[iface].ipv6.empty()) {
						if (nullptr != inet_ntop(family, &(reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr)->sin6_addr), ip, IPBUFFER_MAXSIZE)) {
							net[iface].ipv6 = ip;
						} else {
							int errsv = errno;
							Logger::error("Net::collect() -> Failed to convert IPv6 to string for iface {}, errno: {}", iface, strerror(errsv));
						}
					}
				}  //else, ignoring family==AF_LINK
			}

			//? Get total received and transmitted bytes + device address if no ip was found
			for (const auto &iface : interfaces) {
				for (const string dir : {"download", "upload"}) {
					auto &saved_stat = net.at(iface).stat.at(dir);
					auto &bandwidth = net.at(iface).bandwidth.at(dir);
					uint64_t val = dir == "download" ? std::get<0>(ifstats[iface]) : std::get<1>(ifstats[iface]);

					//? Update speed, total and top values
					if (val < saved_stat.last) {
						saved_stat.rollover += saved_stat.last;
						saved_stat.last = 0;
					}
					if (cmp_greater((unsigned long long)saved_stat.rollover + (unsigned long long)val, numeric_limits<uint64_t>::max())) {
						saved_stat.rollover = 0;
						saved_stat.last = 0;
					}
					saved_stat.speed = round((double)(val - saved_stat.last) / ((double)(new_timestamp - timestamp) / 1000));
					if (saved_stat.speed > saved_stat.top) saved_stat.top = saved_stat.speed;
					if (saved_stat.offset > val + saved_stat.rollover) saved_stat.offset = 0;
					saved_stat.total = (val + saved_stat.rollover) - saved_stat.offset;
					saved_stat.last = val;

					//? Add values to graph
					bandwidth.push_back(saved_stat.speed);
					while (cmp_greater(bandwidth.size(), width * 2)) bandwidth.pop_front();

					//? Set counters for auto scaling
					if (net_auto and selected_iface == iface) {
						if (saved_stat.speed > graph_max[dir]) {
							++max_count[dir][0];
							if (max_count[dir][1] > 0) --max_count[dir][1];
						} else if (graph_max[dir] > 10 << 10 and saved_stat.speed < graph_max[dir] / 10) {
							++max_count[dir][1];
							if (max_count[dir][0] > 0) --max_count[dir][0];
						}
					}
				}
			}

			//? Clean up net map if needed
			if (net.size() > interfaces.size()) {
				for (auto it = net.begin(); it != net.end();) {
					if (not v_contains(interfaces, it->first))
						it = net.erase(it);
					else
						it++;
				}
			}

			timestamp = new_timestamp;
		}
		//? Return empty net_info struct if no interfaces was found
		if (net.empty())
			return empty_net;

		//? Find an interface to display if selected isn't set or valid
		if (selected_iface.empty() or not v_contains(interfaces, selected_iface)) {
			max_count["download"][0] = max_count["download"][1] = max_count["upload"][0] = max_count["upload"][1] = 0;
			redraw = true;
			if (net_auto) rescale = true;
			if (not config_iface.empty() and v_contains(interfaces, config_iface))
				selected_iface = config_iface;
			else {
				//? Sort interfaces by total upload + download bytes
				auto sorted_interfaces = interfaces;
				rng::sort(sorted_interfaces, [&](const auto &a, const auto &b) {
					return cmp_greater(net.at(a).stat["download"].total + net.at(a).stat["upload"].total,
									   net.at(b).stat["download"].total + net.at(b).stat["upload"].total);
				});
				selected_iface.clear();
				//? Try to set to a connected interface
				for (const auto &iface : sorted_interfaces) {
					if (net.at(iface).connected) selected_iface = iface;
					break;
				}
				//? If no interface is connected set to first available
				if (selected_iface.empty() and not sorted_interfaces.empty())
					selected_iface = sorted_interfaces.at(0);
				else if (sorted_interfaces.empty())
					return empty_net;
			}
		}

		//? Calculate max scale for graphs if needed
		if (net_auto) {
			bool sync = false;
			for (const auto &dir : {"download", "upload"}) {
				for (const auto &sel : {0, 1}) {
					if (rescale or max_count[dir][sel] >= 5) {
						const long long avg_speed = (net[selected_iface].bandwidth[dir].size() > 5
														? std::accumulate(net.at(selected_iface).bandwidth.at(dir).rbegin(), net.at(selected_iface).bandwidth.at(dir).rbegin() + 5, 0ll) / 5
														: net[selected_iface].stat[dir].speed);
						graph_max[dir] = max(uint64_t(avg_speed * (sel == 0 ? 1.3 : 3.0)), (uint64_t)10 << 10);
						max_count[dir][0] = max_count[dir][1] = 0;
						redraw = true;
						if (net_sync) sync = true;
						break;
					}
				}
				//? Sync download/upload graphs if enabled
				if (sync) {
					const auto other = (string(dir) == "upload" ? "download" : "upload");
					graph_max[other] = graph_max[dir];
					max_count[other][0] = max_count[other][1] = 0;
					break;
				}
			}
		}

		rescale = false;
		return net.at(selected_iface);
	}
}  // namespace Net

namespace Proc {

	vector<proc_info> current_procs;
	std::unordered_map<uid_t, string> uid_user;
	string current_sort;
	string current_filter;
	bool current_rev = false;
	bool is_tree_mode;

	fs::file_time_type passwd_time;

	uint64_t cputimes;
	int collapse = -1, expand = -1, toggle_children = -1, collapse_all = -1;
	uint64_t old_cputimes = 0;
	atomic<int> numpids = 0;
	int filter_found = 0;

	detail_container detailed;
	static std::unordered_set<size_t> dead_procs;
	//? I/O character count (pi_ioch) of the process shown in the details box
	static long long detailed_ioch = -1;

	string get_status(char s) {
		switch (s) {
			case 'R': return "Running";
			case 'S': return "Sleeping";
			case 'D': return "Waiting";
			case 'Z': return "Zombie";
			case 'T': return "Stopped";
			case 'X': return "Dead";
			default: return "Unknown";
		}
	}

	//? Map procentry64::pi_state to the btop one-letter state
	char state_char(unsigned pi_state, bool used_cpu) {
		switch (pi_state) {
			case AixProc::STATE_ACTIVE: return used_cpu ? 'R' : 'S';
			case AixProc::STATE_ZOMB: return 'Z';
			case AixProc::STATE_STOP: return 'T';
			case AixProc::STATE_IDL: return 'S';
			case AixProc::STATE_SWAP: return 'D';
			default: return 'W';
		}
	}

	//* Get detailed info for selected process
	void _collect_details(const size_t pid, vector<proc_info> &procs) {
		if (pid != detailed.last_pid) {
			detailed = {};
			detailed.last_pid = pid;
			detailed.skip_smaps = not Config::getB("proc_info_smaps");
		}

		//? Copy proc_info for process from proc vector
		auto p_info = rng::find(procs, pid, &proc_info::pid);
		detailed.entry = *p_info;

		//? Update cpu percent deque for process cpu graph
		if (not Config::getB("proc_per_core")) detailed.entry.cpu_p *= Shared::coreCount;
		detailed.cpu_percent.push_back(clamp((long long)round(detailed.entry.cpu_p), 0ll, 100ll));
		while (cmp_greater(detailed.cpu_percent.size(), width)) detailed.cpu_percent.pop_front();

		//? Process runtime : current time - start time (both in unix time - seconds since epoch)
		struct timeval currentTime;
		gettimeofday(&currentTime, nullptr);
		if (detailed.entry.state != 'X') detailed.elapsed = sec_to_dhms(max((long long)0, (long long)currentTime.tv_sec - (long long)detailed.entry.cpu_s));
		else detailed.elapsed = sec_to_dhms(detailed.entry.death_time);
		if (detailed.elapsed.size() > 8) detailed.elapsed.resize(detailed.elapsed.size() - 3);

		//? Get parent process name
		if (detailed.parent.empty()) {
			auto p_entry = rng::find(procs, detailed.entry.ppid, &proc_info::pid);
			if (p_entry != procs.end()) detailed.parent = p_entry->name;
		}

		//? Expand process status from single char to explanative string
		detailed.status = get_status(detailed.entry.state);

		detailed.mem_bytes.push_back(detailed.entry.mem);
		detailed.memory = floating_humanizer(detailed.entry.mem);

		if (detailed.first_mem == -1 or detailed.first_mem < detailed.mem_bytes.back() / 2 or detailed.first_mem > detailed.mem_bytes.back() * 4) {
			detailed.first_mem = min((uint64_t)detailed.mem_bytes.back() * 2, Mem::get_totalMem());
			redraw = true;
		}

		while (cmp_greater(detailed.mem_bytes.size(), width)) detailed.mem_bytes.pop_front();

		//? AIX only accounts the total number of characters transferred (pi_ioch), no read/write split
		if (detailed_ioch >= 0) {
			detailed.io_read = floating_humanizer((uint64_t)detailed_ioch);
			detailed.io_write = "n/a";
		}
	}

	//* Collects and sorts process information from getprocs64()
	auto collect(bool no_update) -> vector<proc_info> & {
		const auto &sorting = Config::getS("proc_sorting");
		auto reverse = Config::getB("proc_reversed");
		const auto &filter = Config::getS("proc_filter");
		auto per_core = Config::getB("proc_per_core");
		auto tree = Config::getB("proc_tree");
		auto show_detailed = Config::getB("show_detailed");
		const auto pause_proc_list = Config::getB("pause_proc_list");
		const size_t detailed_pid = Config::getI("detailed_pid");
		bool should_filter = current_filter != filter;
		if (should_filter) current_filter = filter;
		bool sorted_change = (sorting != current_sort or reverse != current_rev or should_filter);
		bool tree_mode_change = tree != is_tree_mode;
		if (sorted_change) {
			current_sort = sorting;
			current_rev = reverse;
		}
		if (tree_mode_change) is_tree_mode = tree;

		const int cmult = (per_core) ? Shared::coreCount : 1;
		bool got_detailed = false;

		static vector<size_t> found;

		//? Total CPU ticks over all logical CPUs, same source as Cpu::collect()
		{
			perfstat_cpu_total_t ct{};
			if (AixPerfstat::cpu_total(ct))
				cputimes = ct.user + ct.sys + ct.idle + ct.wait;
			else
				cputimes = Cpu::total_ticks;
		}

		//* Use pids from last update if only changing filter, sorting or tree options
		if (no_update and not current_procs.empty()) {
			if (show_detailed and detailed_pid != detailed.last_pid) _collect_details(detailed_pid, current_procs);
		} else {
			//* ---------------------------------------------Collection start----------------------------------------------

			should_filter = true;
			found.clear();
			struct timeval currentTime;
			gettimeofday(&currentTime, nullptr);
			const double timeNow = currentTime.tv_sec + (currentTime.tv_usec / 1'000'000.0);

			//? Read the process table in chunks
			static vector<struct procentry64> entries(512);
			static vector<char> args_buf(8192);
			pid_t index = 0;
			while (true) {
				const int n = getprocs64(entries.data(), sizeof(struct procentry64), nullptr, 0, &index, (int)entries.size());
				if (n < 0) {
					Logger::error("getprocs64() failed: {}", strerror(errno));
					break;
				}
				for (int i = 0; i < n; i++) {
					auto &pe = entries[i];
					const size_t pid = (size_t)pe.pi_pid;
					if (pid < 1) continue;
					//? Skip the per-CPU idle kernel processes; they would show up as 100% CPU consumers
					if ((pe.pi_flags & AixProc::FLAG_KPROC) and std::strncmp(pe.pi_comm, "wait", sizeof pe.pi_comm) == 0) continue;
					found.push_back(pid);

					//? Check if pid already exists in current_procs
					bool no_cache = false;
					auto find_old = rng::find(current_procs, pid, &proc_info::pid);
					//? Only add new processes if not paused
					if (find_old == current_procs.end()) {
						if (not pause_proc_list) {
							current_procs.push_back({pid});
							find_old = current_procs.end() - 1;
							no_cache = true;
						}
						else continue;
					}
					else if (dead_procs.contains(pid)) continue;

					auto &new_proc = *find_old;

					//? Get program name, command, username, parent pid, nice and status
					if (no_cache) {
						new_proc.name = string(pe.pi_comm, strnlen(pe.pi_comm, sizeof pe.pi_comm));
						//? getargs() delivers NUL separated arguments, terminated by an empty string
						if (getargs(&pe, sizeof pe, args_buf.data(), (int)args_buf.size()) == 0) {
							const char* p = args_buf.data();
							const char* end = p + args_buf.size();
							while (p < end and *p != '\0' and cmp_less(new_proc.cmd.size(), 1000)) {
								const size_t len = strnlen(p, end - p);
								new_proc.cmd.append(p, len);
								new_proc.cmd += ' ';
								p += len + 1;
							}
							if (not new_proc.cmd.empty()) new_proc.cmd.pop_back();
						}
						if (new_proc.cmd.empty()) new_proc.cmd = new_proc.name;
						if (new_proc.cmd.size() > 1000) {
							new_proc.cmd.resize(1000);
							new_proc.cmd.shrink_to_fit();
						}
						new_proc.ppid = pe.pi_ppid;
						new_proc.cpu_s = (uint64_t)pe.pi_start;
						if (auto it = uid_user.find(pe.pi_uid); it != uid_user.end()) {
							new_proc.user = it->second;
						} else {
							struct passwd *pwd = getpwuid(pe.pi_uid);
							new_proc.user = (pwd != nullptr ? pwd->pw_name : std::to_string(pe.pi_uid));
							uid_user[pe.pi_uid] = new_proc.user;
						}
					}
					new_proc.p_nice = (int64_t)pe.pi_nice - AixProc::NICE_OFFSET;

					//? CPU time in clock ticks (user + system). The tv_usec fields of procentry64.pi_ru hold
					//? nanoseconds, not microseconds (measured with tools/aix/probes/rusage_probe.c: 147220062 per
					//? half second of a busy loop); read as microseconds a busy loop showed 3200 % on 32 CPUs.
					const uint64_t cpu_ns = (uint64_t)(pe.pi_ru.ru_utime.tv_sec + pe.pi_ru.ru_stime.tv_sec) * 1'000'000'000
						+ (uint64_t)pe.pi_ru.ru_utime.tv_usec + (uint64_t)pe.pi_ru.ru_stime.tv_usec;
					const uint64_t cpu_t = cpu_ns * (uint64_t)Shared::clk_tck / 1'000'000'000;

					new_proc.state = state_char(pe.pi_state, cpu_t > new_proc.cpu_t);

					//? Resident set = data + text pages (4 KiB units, as ps(1) reports RSS)
					new_proc.mem = (pe.pi_drss + pe.pi_trss) * AixProc::PERFSTAT_PAGE;
					new_proc.threads = pe.pi_thcount;

					//? Process cpu usage since last update
					if (cputimes > old_cputimes)
						new_proc.cpu_p = clamp(round(cmult * 1000.0 * (double)(cpu_t - new_proc.cpu_t) / (double)(cputimes - old_cputimes)) / 10.0, 0.0, 100.0 * Shared::coreCount);
					else
						new_proc.cpu_p = 0.0;

					//? Process cumulative cpu usage since process start
					new_proc.cpu_c = (double)cpu_t / max(1.0, (timeNow - (double)new_proc.cpu_s) * Shared::clk_tck);

					//? Update cached value with latest cpu times
					new_proc.cpu_t = cpu_t;

					if (show_detailed and not got_detailed and new_proc.pid == detailed_pid) {
						got_detailed = true;
						detailed_ioch = pe.pi_ioch;
					}
				}
				if (n < (int)entries.size()) break;
			}

			//? Clear dead processes from current_procs if not paused
			if (not pause_proc_list) {
				auto eraser = rng::remove_if(current_procs, [&](const auto& element) { return not v_contains(found, element.pid); });
				current_procs.erase(eraser.begin(), eraser.end());
				if (!dead_procs.empty()) dead_procs.clear();
			}
			//? Set correct state of dead processes if paused
			else {
				const bool keep_dead_proc_usage = Config::getB("keep_dead_proc_usage");
				for (auto& r : current_procs) {
					if (rng::find(found, r.pid) == found.end()) {
						if (r.state != 'X') {
							r.death_time = currentTime.tv_sec - r.cpu_s;
						}
						r.state = 'X';
						dead_procs.emplace(r.pid);
						//? Reset cpu usage for dead processes if paused and option is set
						if (!keep_dead_proc_usage) {
							r.cpu_p = 0.0;
							r.mem = 0;
						}
					}
				}
			}

			//? Update the details info box for process if active
			if (show_detailed and got_detailed) {
				_collect_details(detailed_pid, current_procs);
			} else if (show_detailed and not got_detailed and detailed.status != "Dead") {
				detailed.status = "Dead";
				redraw = true;
			}

			old_cputimes = cputimes;

		}

		//* ---------------------------------------------Collection done-----------------------------------------------

		//* Match filter if defined
		if (should_filter) {
			filter_found = 0;
			for (auto& p : current_procs) {
				if (not tree and not filter.empty()) {
					if (!matches_filter(p, filter)) {
						p.filtered = true;
						filter_found++;
					} else {
						p.filtered = false;
					}
				} else {
					p.filtered = false;
				}
			}
		}

		//* Sort processes
		if ((sorted_change or tree_mode_change) or (not no_update and not pause_proc_list)) {
			proc_sorter(current_procs, sorting, reverse, tree);
		}

		//* Generate tree view if enabled
		if (tree and (not no_update or should_filter or sorted_change)) {
			bool locate_selection = false;

			if (toggle_children != -1) {
				auto collapser = rng::find(current_procs, toggle_children, &proc_info::pid);
				if (collapser != current_procs.end()){
					for (auto& p : current_procs) {
						if (p.ppid == collapser->pid) {
							auto child = rng::find(current_procs, p.pid, &proc_info::pid);
							if (child != current_procs.end()){
								child->collapsed = not child->collapsed;
							}
						}
					}
					if (Config::ints.at("proc_selected") > 0) locate_selection = true;
				}
				toggle_children = -1;
			}

			if (auto find_pid = (collapse != -1 ? collapse : expand); find_pid != -1) {
				auto collapser = rng::find(current_procs, find_pid, &proc_info::pid);
				if (collapser != current_procs.end()) {
					if (collapse == expand) {
						collapser->collapsed = not collapser->collapsed;
					}
					else if (collapse > -1) {
						collapser->collapsed = true;
					}
					else if (expand > -1) {
						collapser->collapsed = false;
					}
					if (Config::ints.at("proc_selected") > 0) locate_selection = true;
				}
				collapse = expand = -1;
			}

			if (collapse_all != -1) {
				toggle_tree_collapse(current_procs);
				collapse_all = -1;
				if (Config::ints.at("proc_selected") > 0) locate_selection = true;
			}

			if (should_filter or not filter.empty()) filter_found = 0;

			vector<tree_proc> tree_procs;
			tree_procs.reserve(current_procs.size());

			if (!pause_proc_list) {
				for (auto& p : current_procs) {
					if (not v_contains(found, p.ppid)) p.ppid = 0;
				}
			}

			//? Stable sort to retain selected sorting among processes with the same parent
			rng::stable_sort(current_procs, rng::less{}, & proc_info::ppid);

			//? Auto-collapse processes with many children when entering tree mode
			_auto_collapse_oversized(current_procs, tree_mode_change);

			//? Start recursive iteration over processes with the lowest shared parent pids
			for (auto& p : rng::equal_range(current_procs, current_procs.at(0).ppid, rng::less{}, &proc_info::ppid)) {
				_tree_gen(p, current_procs, tree_procs, 0, false, filter, false, no_update, should_filter);
			}

			//? Recursive sort over tree structure to account for collapsed processes in the tree
			int index = 0;
			tree_sort(tree_procs, sorting, reverse, (pause_proc_list and not (sorted_change or tree_mode_change)), index, current_procs.size());

			//? Recursive construction of ASCII tree prefixes.
			for (auto t = tree_procs.begin(); t != tree_procs.end(); ++t) {
				_collect_prefixes(*t, t == tree_procs.end() - 1);
			}

			//? Final sort based on tree index
			rng::stable_sort(current_procs, rng::less {}, &proc_info::tree_index);

			//? Move current selection/view to the selected process when collapsing/expanding in the tree
			if (locate_selection) {
				int loc = rng::find(current_procs, Proc::selected_pid, &proc_info::pid)->tree_index;
				if (Config::ints.at("proc_start") >= loc or Config::ints.at("proc_start") <= loc - Proc::select_max)
					Config::ints.at("proc_start") = max(0, loc - 1);
				Config::ints.at("proc_selected") = loc - Config::ints.at("proc_start") + 1;
			}
		}

		numpids = (int)current_procs.size() - filter_found;
		return current_procs;
	}
}  // namespace Proc

namespace Tools {
	double system_uptime() {
		perfstat_cpu_total_t ct{};
		if (AixPerfstat::cpu_total(ct) and ct.lbolt > 0 and Shared::clk_tck > 0)
			return (double)ct.lbolt / (double)Shared::clk_tck;
		return (double)(time(nullptr) - Shared::bootTime);
	}
}  // namespace Tools


# EPYC™ System Management Interface (E-SMI) In-band Library

The EPYC™ System Management Interface In-band Library, or E-SMI library, is part of the EPYC™ System Management Inband software stack. It is a C library for Linux that provides a user space interface to monitor and control the CPU's power, energy, performance and other system management features.

## Important note about Versioning and Backward Compatibility
The E-SMI library is currently under development, and therefore subject to change at the API level. The intention is to keep the API as stable as possible while in development, but in some cases we may need to break backwards compatibility in order to achieve future stability and usability. Following Semantic Versioning rules, while the E-SMI library is in a high state of change, the major version will remain 0, and achieving backward compatibility may not be possible.

Once new development has leveled off, the major version will become greater than 0, and backward compatibility will be enforced between major versions.

# Building E-SMI

## Dowloading the source
The source code for E-SMI library is available on [Github](https://github.com/amd/esmi_ib_library).

## Directory stucture of the source
Once the E-SMI library source has been cloned to a local Linux machine, the directory structure of source is as below:
* `$ docs/` Contains Doxygen configuration files and Library descriptions
* `$ tools/` Contains e-smi tool, based on the E-SMI library
* `$ include/` Contains the header files used by the E-SMI library
* `$ src/` Contains library E-SMI source

## Building the library and tool
Building the library is achieved by following the typical CMake build sequence, as follows.
#### ```$ mkdir -p build```
#### ```$ cd build```
#### ```$ cmake <location of root of E-SMI library CMakeLists.txt>```

## Building the library for static linking
Building the library as a Static(.a) along with shared libraries(.so) is achieved by following sequence.
The static library is part of RPM and DEB package when compiled with cmake as below and built with 'make package'.
The next step can be skipped if static lib support is not required
#### ```$ cmake -DENABLE_STATIC_LIB=1 <location of root of E-SMI library CMakeLists.txt>```

#### ```$ make```
The built library `libe_smi64.so.X.Y` will appear in the `build` folder.

#### ```# Install library file and header; default location is /opt/e-sms```
#### ```$ sudo make install```

## Building the Documentation
The documentation PDF file can be built with the following steps (continued from the steps above):
#### ```$ make doc```
Upon a successful build, the `ESMI_Manual.pdf` and `ESMI_IB_Release_Notes.pdf` will be copied to the top directory of the source.

## Building the package
The RPM and DEB packages can be created with the following steps (continued from the steps above):
#### ```$ make package```

# Kernel dependencies
The E-SMI Library depends on the following device drivers from Linux to manage the system management features.

## Monitoring energy counters
The Energy counters reported by the RAPL MSRs, the AMD Energy driver exposes the per core and per socket counters via the HWMON sys entries. The AMD Energy driver is upstreamed and is available as part of Linux v5.8+. The kernel config symbol SENSORS_AMD_ENERGY needs to be selected, can be built and inserted as a module.

## Monitoring and managing power metrics, boostlimits and other system management features
The power metrics, boostlimits and other features are managed by the SMU firmware and exposed via PCI config space. AMD provides Linux kernel module exposing this information to the user-space via sys entries.

* amd_hsmp driver is accepted upstream under drivers/platform/x86 is availble https://git.kernel.org/pub/scm/linux/kernel/git/pdx86/platform-drivers-x86.git/commit/?h=for-next&id=91f410aa679a035e7abdff47daca4418c384c770
  * Please build the library against uapi header asm/amd_hsmp.h

* PCIe interface needs to be enabled in the BIOS. On the reference BIOS, the CBS option may be found in the following path
#####  ```Advanced > AMD CBS > NBIO Common Options > SMU Common Options > HSMP Support```
#####  ```	BIOS Default:     “Auto” (Disabled)```

  If the option is disabled, the related E-SMI APIs will return -ETIMEDOUT.

## Supported hardware
AMD Zen3 based CPU Family `19h` Models `0h-Fh` and `30h-3Fh`.

## Additional required software for building
In order to build the E-SMI library, the following components are required. Note that the software versions listed are what is being used in development. Earlier versions are not guaranteed to work:
* CMake (v3.5.0)

In order to build the latest documentation, the following are required:

* DOxygen (1.8.13)
* latex (pdfTeX 3.14159265-2.6-1.40.18)

# Usage Basics
## Device Indices
Many of the functions in the library take a "core/socket index". The core/socket index is a number greater than or equal to 0, and less than the number of cores/sockets on the system.

### Hello E-SMI
The only required E-SMI call for any program that wants to use E-SMI is the `esmi_init()` call. This call initializes some internal data structures that will be used by subsequent E-SMI calls.

When E-SMI is no longer being used, `esmi_exit()` should be called. This provides a way to do any releasing of resources that E-SMI may have held. In many cases, this may have no effect, but may be necessary in future versions of the library.

Below is a simple "Hello World" type program that display the Average Power of Sockets.

```
#include <stdio.h>
#include <stdint.h>
#include <e_smi/e_smi.h>
#include <e_smi/e_smi_monitor.h>

int main()
{
	esmi_status_t ret;
	unsigned int i;
	uint32_t power;
	uint32_t total_sockets = 0;

	ret = esmi_init();
	if (ret != ESMI_SUCCESS) {
		printf("ESMI Not initialized, drivers not found.\n"
			"Err[%d]: %s\n", ret, esmi_get_err_msg(ret));
		return ret;
	}

	total_sockets = esmi_get_number_of_sockets();
	for (i = 0; i < total_sockets; i++) {
		power = 0;
		ret = esmi_socket_power_get(i, &power);
		if (ret != ESMI_SUCCESS) {
			printf("Failed to get socket[%d] avg_power, "
				"Err[%d]:%s\n", i, ret, esmi_get_err_msg(ret));
		}
		printf("socket_%d_avgpower = %.3f Watts\n",
			i, (double)power/1000);
	}
	esmi_exit();

	return ret;
}
```
# Usage
## Tool Usage
E-SMI tool is a C program based on the E-SMI In-band Library, the executable "e_smi_tool" will be generated in the build/ folder.
This tool provides options to Monitor and Control System Management functionality.

Below is a sample usage to dump the functionality, with default core/socket/package as 0.
```
	e_smi_library/build> sudo ./e_smi_tool

	=============== EPYC System Management Interface ===============

	--------------------------------------
	| CPU Family            | 0x19 (25 ) |
	| CPU Model             | 0x0  (0  ) |
	| NR_CPUS               | 128        |
	| NR_SOCKETS            | 2          |
	| THREADS PER CORE      | 1 (SMT OFF)|
	--------------------------------------

	----------------------------------------------------------------
	| Sensor Name            | Socket 0         | Socket 1         |
	----------------------------------------------------------------
	| Energy (K Joules)      | 206.088          | 212.171          |
	| Power (Watts)          | 42.224           | 42.634           |
	| PowerLimit (Watts)     | 200.000          | 120.000          |
	| PowerLimitMax (Watts)  | 225.000          | 225.000          |
	| C0 Residency (%)       | 0                | 0                |
	----------------------------------------------------------------
	| Core[0] Energy (Joules)| 6.123            | 5.520            |
	| Core[0] boostlimit(MHz)| 2500             | 2000             |
	----------------------------------------------------------------


	Try `./e_smi_tool --help' for more information.
```

For detailed and up to date usage information, we recommend consulting the help:

For convenience purposes, following is the output from the -h flag:
```
	e_smi_library/build> ./e_smi_tool --help

	=============== EPYC System Management Interface ===============

	Usage: ./e_smi_tool [Option]... <INPUT>...

	Output Option<s>:
	  -h, --help						Show this help message
	  -A, --showall						Get all esmi parameter Values

	Get Option<s>:
	  -e, --showcoreenergy [CORE]				Get energy for a given CPU (Joules)
	  -s, --showsockenergy					Get energy for all sockets (KJoules)
	  -p, --showsockpower					Get power metrics for all sockets (mWatts)
	  -L, --showcorebl [CORE]				Get Boostlimit for a given CPU (MHz)
	  -r, --showsockc0res [SOCKET]				Get c0_residency for a given socket (%)
	  -d, --showddrbw					Show DDR bandwidth details (Gbps)
	  -t, --showsockettemp					Show Temperature monitor of socket (°C)
	  --showsmufwver					Show SMU FW Version
	  --showhsmpprotover					Show HSMP Protocol Version
	  --showprochotstatus					Show HSMP PROCHOT status (in/active)
	  --showclocks						Show (CPU, Mem & Fabric) clock frequencies (MHz)

	Set Option<s>:
	  -C, --setpowerlimit [SOCKET] [POWER]			Set power limit for a given socket (mWatts)
	  -a, --setcorebl [CORE] [BOOSTLIMIT]			Set boost limit for a given core (MHz)
	  --setsockbl [SOCKET] [BOOSTLIMIT]			Set Boost limit for a given Socket (MHz)
	  --setpkgbl [BOOSTLIMIT]				Set Boost limit for all sockets in a package (MHz)
	  --apbdisable [SOCKET] [PSTATE]			Set Data Fabric Pstate for a given socket, PSTATE = 0 to 3
	  --apbenable [SOCKET]					Enable the Data Fabric performance boost algorithm for a given socket
	  --setxgmiwidth [MIN] [MAX]				Set xgmi link width in a multi socket system, MIN = MAX = 0 to 2
	  --setlclkdpmlevel [SOCKET] [NBIOID] [MIN] [MAX]	Set lclk dpm level for a given nbio, given socket, MIN = MAX = NBIOID = 0 to 3

	====================== End of EPYC SMI Log =====================
```

Below is a sample usage to get the individual library functionality API's.
We can pass arguments in short or long options ex: "./e_smi_tool -e 0" or "./e_smi_tool --showcoreenergy 0"
```
1.	e_smi_library/build> ./e_smi_tool -e 0

	=============== EPYC System Management Interface ===============

	core[0] energy  :         17211.219 Joules

	====================== End of EPYC SMI Log =====================

2.	e_smi_library/build> ./e_smi_tool --showcoreenergy 0

	=============== EPYC System Management Interface ===============

	core[0] energy  :         17216.800 Joules

	====================== End of EPYC SMI Log =====================

3.      e_smi_library/build>./e_smi_tool -e 12 --showsockpower --setpowerlimit 1 220000 -p

	=============== EPYC System Management Interface ===============

	core[12] energy :           246.251 Joules

	----------------------------------------------------------------
	| Sensor Name            | Socket 0         | Socket 1         |
	----------------------------------------------------------------
	| Power (Watts)          | 66.508           | 67.548           |
	| PowerLimit (Watts)     | 22.000           | 220.000          |
	| PowerLimitMax (Watts)  | 240.000          | 240.000          |
	----------------------------------------------------------------

	Set socket[1] power_limit :         220.000 Watts successfully

	----------------------------------------------------------------
	| Sensor Name            | Socket 0         | Socket 1         |
	----------------------------------------------------------------
	| Power (Watts)          | 66.520           | 67.556           |
	| PowerLimit (Watts)     | 22.000           | 220.000          |
	| PowerLimitMax (Watts)  | 240.000          | 240.000          |
	----------------------------------------------------------------

	====================== End of EPYC SMI Log =====================
```

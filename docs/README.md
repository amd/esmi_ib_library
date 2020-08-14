
# EPYC™ System Management Interface (E-SMI) In-band Library

The EPYC™ System Management Interface In-band Library, or E-SMI library, is part of the EPYC™ System Management Inband software stack. It is a C library for Linux that provides a user space interface to monitor and control the CPU's Power, Energy and Performance.

# Important note about Versioning and Backward Compatibility
The E-SMI library is currently under development, and therefore subject to change at the API level. The intention is to keep the API as stable as possible while in development, but in some cases we may need to break backwards compatibility in order to achieve future stability and usability. Following Semantic Versioning rules, while the E-SMI library is in a high state of change, the major version will remain 0, and achieving backward compatibility may not be possible.

Once new development has leveled off, the major version will become greater than 0, and backward compatibility will be enforced between major versions.

# Building E-SMI

#### Additional Required software for building
In order to build the E-SMI library, the following components are required. Note that the software versions listed are what is being used in development. Earlier versions are not guaranteed to work:
* CMake (v3.5.0)

#### Dowloading the source
The source code for E-SMI library is available on [Github](https://github.com/amd/esmi_ib_library).

#### Directory stucture of the source
Once the E-SMI library source has been cloned to a local Linux machine, the directory structure of source is as below:
* `$ docs/` Contains Doxygen configuration files and Library descriptions
* `$ example/` Contains e-smi tool, based on the E-SMI library
* `$ include/` Contains the header files used by the E-SMI library
* `$ src/` Contains library E-SMI source

Building the library is achieved by following the typical CMake build sequence, as follows.
##### ```$ mkdir -p build```
##### ```$ cd build```
##### ```$ cmake <location of root of E-SMI library CMakeLists.txt>```
##### ```$ make```
The built library will appear in the `build` folder.

#### Building the Documentation
The documentation PDF file can be built with the following steps (continued from the steps above):
##### ```$ make doc```
The reference manual, `refman.pdf` will be in the `latex` directory and `refman.rtf` will be in the `rtf` directory upon a successful build.

# Dependencies
The E-SMI Library depends on the following device drivers from Linux to manage the system management features.

## Monitoring Energy counters
The Energy counters are exposed via the RAPL MSRs and the AMD Energy driver exposes the per core and per socket information via the HWMON sys entries. The AMD Energy driver is upstreamed and available as part of Linux v5.8, this driver may be insmoded as a module.

## Monitoring and Managing Power metrics, Boostlimits
The power metrics and Boostlimits features are managed by the SMU firmware and exposed via SMN PCI config space. AMD provided Linux HSMP driver exposes this information to the user-space via sys entries.

# Usage Basics
## Device Indices
Many of the functions in the library take a "core/socket index". The core/socket index is a number greater than or equal to 0, and less than the number of cores/sockets on the system.

# Hello E-SMI
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
		ret = esmi_socket_power_avg_get(i, &power);
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
	e_smi_library/build> ./e_smi_tool

	===============EPYC System Management Interface===============

	_TOPOLOGY       | Count |
	#CPUS           |   256 |
	#SOCKETS        |     2 |

	Considered Default 'CORE/SOCKET/PKG ID's are 0
	_SENSOR NAME            |       Value Units         |
	_CORE_ENERGY            |      3156295 uJoules      |
	_SOCKET_ENERGY          |  38700978759 uJoules      |
	_SOCKET_AVG_POWER       |       56.220 Watts        |
	_SOCKET_POWERCAP        |      200.000 Watts        |
	_SOCKET_MAX_POWERCAP    |      240.000 Watts        |
	_CORE_BOOSTLIMIT        |         3200 MHz          |

	======================End of EPYC SMI Log=====================

	Try './e_smi_tool --help' for more information.
```

For detailed and up to date usage information, we recommend consulting the help:

For convenience purposes, following is the output from the -h flag:
```
	e_smi_library/build> ./e_smi_tool --help
	Usage: ./e_smi_tool [Option<s>] SOURCES
	Option<s>:
		-A, (--showall)						Get all esmi parameter Values
		-e, (--showcoreenergy)    [CORENUM]			Get energy for a given CPU
		-s, (--showsocketenergy)  [SOCKETNUM]			Get energy for a given socket
		-p, (--showsocketpower)   [SOCKETNUM]			Get power params for a given socket
		-L, (--showcoreboostlimit)[CORENUM]			Get Boostlimit for a given CPU
		-C, (--setpowercap)       [SOCKETNUM] [POWERVALUE]      Set power Cap for a given socket
		-a, (--setcoreboostlimit) [CORENUM] [BOOSTVALUE]	Set boost limit for a given core
		-b, (--setsocketboostlimit)[SOCKETNUM] [BOOSTVALUE]     Set Boost limit for a given Socket
		-c, (--setpkgboostlimit)  [PKG_BOOSTLIMIT]		Set Boost limit for a given package
		-h, (--help)						Show this help message
```

Below is a sample usage to get the individual library functionality API's.
We can pass arguments either any of the ways "./e_smi_tool -e 0" or "./e_smi_tool --showcoreenergy=0"
```
1.	e_smi_library/build> ./e_smi_tool -e 0
	===============EPYC System Management Interface===============

	hwmon/core_energy[0]_input:     505525 uJoules

	======================End of EPYC SMI Log=====================

2.	e_smi_library/build> ./e_smi_tool --showcoreenergy=0
	===============EPYC System Management Interface===============

	hwmon/core_energy[0]_input:     41505525 uJoules

	======================End of EPYC SMI Log=====================

3.      e_smi_library/build> ./e_smi_tool -e 12 --showsocketpower=1 --setpowercap 1 230000 -p 1

	===============EPYC System Management Interface===============

	hwmon/core_energy[12]_input:      651357 uJoules
	socket[1]/avg_power     :         54.218 Watts
	socket[1]/power_cap     :         220.000 Watts
	socket[1]/power_cap_max :         240.000 Watts
	Set socket[1]/power_cap :         230.000 Watts successfully
	socket[1]/avg_power     :         55.178 Watts
	socket[1]/power_cap     :         230.000 Watts
	socket[1]/power_cap_max :         240.000 Watts

	======================End of EPYC SMI Log=====================
```

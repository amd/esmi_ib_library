
# EPYC™ System Management Interface (E-SMI) In-band Library

The EPYC™ System Management Interface In-band Library, or E-SMI library, is part of the EPYC™ System Management Inband software stack. It is a C library for Linux that provides a user space interface to monitor and control the CPU's power, energy, performance and other system management features.

## Important note about Versioning and Backward Compatibility
The E-SMI library is currently under development, and therefore subject to change at the API level. The intention is to keep the API as stable as possible while in development, but in some cases we may need to break backwards compatibility in order to achieve future stability and usability. Following Semantic Versioning rules, while the E-SMI library is in a high state of change, the major version will remain 0, and achieving backward compatibility may not be possible.

Once new development has leveled off, the major version will become greater than 0, and backward compatibility will be enforced between major versions.

# Building E-SMI

## Dowloading the source
The source code for E-SMI library is available at [Github](https://github.com/amd/esmi_ib_library).

## Directory stucture of the source
Once the E-SMI library source has been cloned to a local Linux machine, the directory structure of source is as below:
* `$ docs/` Contains Doxygen configuration files and Library descriptions
* `$ tools/` Contains e-smi tool, based on the E-SMI library
* `$ include/` Contains the header files used by the E-SMI library
* `$ src/` Contains library E-SMI source
* `$ cmake_modules/` Contains helper utilities for determining package and library version
* `$ DEBIAN/` Contains debian pre and post installation scripts
* `$ RPM/` Contains rpm pre and post installation scripts

## Building the library and tool
Building the library is achieved by following the typical CMake build sequence, as below
* `$ cd <location of root of E-smi library>`
* `$ mkdir -p build`
* `$ cd build`
* `$ cmake ../`

**Building the library for static linking**

Building the library as a static(.a) along with shared libraries(.so) is achieved by following sequence. The static library is part of RPM and DEB package when compiled with cmake as below and built with 'make package'.
* `$ cmake -DENABLE_STATIC_LIB=1 ../`

* `$ make`

 The built library `libe_smi64_static.a`, `libe_smi64.so.X.Y` and `esmi_tool` will appear in the `build` directory

* `$ sudo make install`

 Library file, header and tool are installed at /opt/e-sms

`Note:`
 Library is dependent on amd_hsmp.h header and without this, compilation will break. Please follow the instruction in "Kernel dependencies" section

## Building the Documentation
The documentation PDF file can be built with the following steps (continued from the steps above)
`$ make doc`
Upon a successful build, the `ESMI_Manual.pdf` and `ESMI_IB_Release_Notes.pdf` will be copied to the top directory of the source.

## Building the package
The RPM and DEB packages can be created with the following steps (continued from the steps above):
`$ make package`

# Kernel dependencies
The E-SMI Library depends on the following device drivers from Linux to manage the system management features.

## Monitoring energy counters
* AMD family 19, model 00-0fh and 30-3fh
  * These processors support energy monitoring through 32 bit RAPL MSR registers.
  *`amd_energy` driver, an out of tree kernel module, hosted at [amd_energy](https://github.com/amd/amd_energy) can report per core and per socket counters via the HWMON sysfs entries.
  * This driver provides accumulation of energy for avoiding wrap around problem.
* AMD family 19, model 10-1fh and a0-afh
  * These processors support energy monitoring through 64 bit RAPL MSR registers.
  * Because of 64 bit registers, there is no accumulation of energy needed.
  * For these processors "msr-safe" driver is used from [msr-safe](https://github.com/LLNL/msr-safe.git).
  * Msr-safe driver needs allowlist file to be written to "/dev/cpu/msr_allowlist" for allowing the read of those specific msr registers.
    Please follow below steps or use --writeallowlist tool option.
    * create "amd_allowlist" file with below contents and run the command "sudo su" and "cat amd_allowlist > /dev/cpu/msr_allowlist"
```
# MSR # Write Mask # Comment
0xC0010299 0x0000000000000000 # "ENERGY_PWR_UNIT_MSR"
0xC001029A 0x0000000000000000 # "ENERGY_CORE_MSR"
0xC001029B 0x0000000000000000 # "ENERGY_PKG_MSR"
```

## Monitoring and managing power metrics, boostlimits and other system management features
The power metrics, boostlimits and other features are managed by the SMU(System Management Unit of the processor) firmware and exposed via PCI config space and accessed through "Host System Management Port(HSMP)" at host/cpu side. AMD provides Linux kernel module(amd_hsmp) exposing this information to the user-space via ioctl interface.
* amd_hsmp driver is accepted in upstream kernel and is available at linux tree at drivers/platform/x86/amd/hsmp.c from version 5.17.rc1 onwards
* If you are using a kernel version less than that, then copy the header file from arch/x86/include/uapi/asm/amd_hsmp.h in linux source tree to below locations based on the type of the system.
  * On RHEL systems, path is: /usr/include/asm/
  * On Ubuntu systems path is: /usr/include/x86_64-linux-gnu/asm/
* To get HSMP working. PCIe interface needs to be enabled in the BIOS. On the reference BIOS please follow the sequence below for enabling HSMP.

  **Advanced > AMD CBS > NBIO Common Options > SMU Common Options > HSMP Support**

  **BIOS Default:     “Auto” (Disabled)**
		to
  **BIOS Default:     "Enabled"**

  If the above HSMP support option is disabled, the related E-SMI APIs will return -ETIMEDOUT.

# Supported hardware
AMD Zen3 based CPU Family `19h` Models `0h-Fh` and `30h-3Fh`.\n
AMD Zen4 based CPU Family `19h` Models `10h-1Fh` and `A0-AFh`.\n

# Additional required software for building
In order to build the E-SMI library, the following components are required. Note that the software versions listed are what is being used in development. Earlier versions are not guaranteed to work:
* CMake (v3.5.0)

In order to build the latest documentation, the following are required:

* DOxygen (1.8.13)
* latex (pdfTeX 3.14159265-2.6-1.40.18)

# Library Usage Basics
Many of the functions in the library take a "core/socket index". The core/socket index is a number greater than or equal to 0, and less than the number of cores/sockets on the system. Number of cores/sockets in a system can be obtained from esmi library APIs.

## Hello E-SMI
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
# Tool Usage
E-SMI tool is a C program based on the E-SMI In-band Library, the executable "e_smi_tool" will be generated in the build/ folder.
This tool provides options to Monitor and Control System Management functionality.

Below is a sample usage to dump core and socket metrics
```
	e_smi_library/b$ sudo ./e_smi_tool

	============================= E-SMI ===================================

	--------------------------------------
	| CPU Family            | 0x19 (25 ) |
	| CPU Model             | 0x10 (16 ) |
	| NR_CPUS               | 384        |
	| NR_SOCKETS            | 2          |
	| THREADS PER CORE      | 2 (SMT ON) |
	--------------------------------------

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Energy (K Joules)              | 14437.971        | 14087.151        |
	| Power (Watts)                  | 174.290          | 169.630          |
	| PowerLimit (Watts)             | 400.000          | 320.000          |
	| PowerLimitMax (Watts)          | 400.000          | 320.000          |
	| C0 Residency (%)               | 0                | 0                |
	| DDR Bandwidth                  |                  |                  |
	|       DDR Max BW (GB/s)        | 58               | 58               |
	|       DDR Utilized BW (GB/s)   | 0                | 0                |
	|       DDR Utilized Percent(%)  | 0                | 0                |
	| Current Active Freq limit      |                  |                  |
	|        Freq limit (MHz)        | 3500             | 3500             |
	|        Freq limit source       | Refer below[*0]  | Refer below[*1]  |
	| Socket frequency range         |                  |                  |
	|        Fmax (MHz)              | 3500             | 3500             |
	|        Fmin (MHz)              | 400              | 400              |
	------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU energies in Joules:                                                                                       |
	| cpu [  0] :    645.992    181.415    171.678    165.577    161.001    158.397    161.333    151.716           |
	| cpu [  8] :     88.197     79.306     73.860     73.015     72.960     69.293     67.871     78.895           |
	| cpu [ 16] :     70.376     71.231     61.756     63.061     80.656     73.360     69.566     69.969           |
	| cpu [ 24] :     67.054     65.621     64.468     66.346     64.344     64.310     71.548     65.579           |
	| cpu [ 32] :     65.731     62.931     65.526     69.765     69.050     65.782     70.630     65.282           |
	| cpu [ 40] :     69.608     67.261     63.765     69.477     68.677     63.145     62.451    159.949           |
	| cpu [ 48] :     70.810     73.084     64.584     62.966     66.581     65.620     62.381     65.602           |
	| cpu [ 56] :     72.804     70.842     69.651     64.990     63.924     66.468     63.401    296.924           |
	| cpu [ 64] :     64.693     62.723     65.057     62.515     60.091     60.422     62.217     66.552           |
	| cpu [ 72] :     81.746     70.622     68.848    301.949     78.974     68.130     68.141     65.693           |
	| cpu [ 80] :     77.475     72.441     81.296     71.441     71.988     75.237     73.986     69.467           |
	| cpu [ 88] :     73.385     69.277     61.759     61.060     62.834     60.681     62.835     62.703           |
	| cpu [ 96] :    142.718    139.519    134.449    134.097    135.045    140.307    140.553    137.153           |
	| cpu [104] :     66.016     66.736     62.224     67.137     64.881     70.592     64.701     64.056           |
	| cpu [112] :     70.791     69.107     70.638     69.998     68.199     65.263     70.638     72.557           |
	| cpu [120] :     94.391     94.151     71.881     66.493     64.653     66.141     66.132     69.593           |
	| cpu [128] :     65.800     64.742     63.130     61.771     65.416     66.205     64.663     71.349           |
	| cpu [136] :     72.183     66.754     67.090     63.343     69.450     67.979     68.285     70.478           |
	| cpu [144] :     68.281     63.809     62.717     63.348     71.164     72.289     65.516     65.513           |
	| cpu [152] :     74.588     69.074     66.711     66.011     67.896     65.933     67.031     65.474           |
	| cpu [160] :     66.668     62.996     65.945     63.734     64.060     68.597     76.405     91.436           |
	| cpu [168] :     77.658     70.085     67.025     68.951     64.678     64.821     65.031     71.694           |
	| cpu [176] :     72.782     89.196     74.777     73.703     66.247     65.419     64.748     63.978           |
	| cpu [184] :     63.887     66.080     64.042     65.151     69.661     74.616     63.834     69.824           |
	-----------------------------------------------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU boostlimit in MHz:                                                                                        |
	| cpu [  0] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 16] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 32] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 48] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 64] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 80] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 96] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [112] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [128] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [144] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [160] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [176] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	-----------------------------------------------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU core clock current frequency limit in MHz:                                                                |
	| cpu [  0] : 3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 16] : NA    NA    NA    NA    NA    NA    NA    NA    3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 32] : 3500  3500  3500  3500  3500  3500  3500  3500  NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [ 48] : 3500  3500  3500  3500  3500  3500  3500  3500  NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [ 64] : NA    NA    NA    NA    NA    NA    NA    NA    3500  3500  3500  3500  3500  3500  3500  3500    |
	| cpu [ 80] : NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [ 96] : NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [112] : NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [128] : NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [144] : NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [160] : NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA      |
	| cpu [176] : NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA    NA      |
	-----------------------------------------------------------------------------------------------------------------
	*0 Frequency limit source names:
	OPN Max

	*1 Frequency limit source names:
	OPN Max



	Try `./e_smi_tool --help' for more information.

```

For detailed and up to date usage information, we recommend consulting the help:

For convenience purposes, following is the output from the -h flag:

```
	e_smi_library/b$ ./e_smi_tool -h

	============================= E-SMI ===================================

	Usage: ./e_smi_tool [Option]... <INPUT>...

	Output Option<s>:
	-h, --help                                                    Show this help message
	-A, --showall                                                 Show all esmi parameter values
	-V  --version                                                 Show e-smi library version
	--testmailbox [SOCKET] [VALUE]                                Test HSMP mailbox interface
	--writemsrallowlist                                           Write msr-safe allowlist file


	Get Option<s>:
	--showcoreenergy [CORE]                                       Show energy for a given CPU (Joules)
	--showsockenergy                                              Show energy for all sockets (KJoules)
	--showsockpower                                               Show power metrics for all sockets (Watts)
	--showcorebl [CORE]                                           Show Boostlimit for a given CPU (MHz)
	--showsockc0res [SOCKET]                                      Show c0_residency for a given socket (%%)
	--showsmufwver                                                Show SMU FW Version
	--showhsmpprotover                                            Show HSMP Protocol Version
	--showprochotstatus                                           Show HSMP PROCHOT status for all sockets
	--showclocks                                                  Show Clock Metrics (MHz) for all sockets
	--showcclkfreqlimit [CORE]                                    Show current clock frequency limit(MHz) for a given core
	--showsvipower                                                Show svi based power telemetry of all rails for all sockets
	--showxgmibw [LINK<P2,P3,G0-G7>] [BW<AGG_BW,RD_BW,WR_BW>]     Show xGMI bandwidth for a given socket, linkname and bwtype
	--showiobw [SOCKET] [LINK<P2,P3,G0-G7>]                       Show IO aggregate bandwidth for a given socket and linkname
	--showlclkdpmlevel [SOCKET] [NBIOID<0-3>]                     Show lclk dpm level for a given nbio in a given socket
	--showsockclkfreqlimit [SOCKET]                               Show current clock frequency limit(MHz) for a given socket
	--showmetrictablever                                          Show Metrics Table Version
	--showmetrictable [SOCKET]                                    Show Metrics Table

	--setpowerlimit [SOCKET] [POWER]                              Set power limit for a given socket (mWatts)
	--setcorebl [CORE] [BOOSTLIMIT]                               Set boost limit for a given core (MHz)
	--setsockbl [SOCKET] [BOOSTLIMIT]                             Set Boost limit for a given Socket (MHz)
	--setxgmiwidth [MIN<0-2>] [MAX<0-2>]                          Set xgmi link width in a multi socket system (MAX >= MIN)
	--setlclkdpmlevel [SOCKET] [NBIOID<0-3>] [MIN<0-3>] [MAX<0-3>]Set lclk dpm level for a given nbio in a given socket (MAX >= MIN)

	============================= End of E-SMI ============================

```
Following are the value ranges and other information needed for passing it to tool
```
1.	--showxgmibw [SOCKET] [LINKNAME] [BWTYPE]

	  LINKNAME :
	  Rolling Stones:P0/P1/P2/P3/G0/G1/G2/G3
	  Mi300A:G0/G1/G2/G3/G4/G5/G6/G7
	  Breithorn:P1/P3/G0/G1/G2/G3

	  BWTYPE : AGG_BW/RD_BW/WR_BW

2.	--setxgmiwidth [MIN] [MAX]

	  MIN : MAX :  0 - 2 with MIN <= MAX

3.	--showlclkdpmlevel [SOCKET] [NBIOID]

	  NBIOID : 0 - 3

4.	--apbdisable [SOCKET] [PSTATE]

	  PSTATE : 0 - 2

5.	--setlclkdpmlevel [SOCKET] [NBIOID] [MIN] [MAX]

	  NBIOID : 0 - 3

	  MIN : MAX : 0 - 3 with MIN <= MAX

6. 	--setpcielinkratecontrol [SOCKET] [CTL]

	  CTL : 0 - 2

7.	--setpowerefficiencymode [SOCKET] [MODE]

	  MODE : 0 - 3

8.	--setdfpstaterange [SOCKET] [MAX] [MIN]

	  MIN : MAX : 0 - 2 with MAX <= MIN

9.	--setgmi3linkwidth [SOCKET] [MIN] [MAX]

	  MIN : MAX : 0 - 2 with MIN <= MAX

10.	--testmailbox [SOCKET] [VALUE]
	  VALUE : Any 32 bit value

```
Below is a sample usage to get different system metrics information
```

1.	e_smi_library/b$ sudo ./e_smi_tool --showcoreenergy 0

	============================= E-SMI ===================================

	-------------------------------------------------
	| core[000] energy  |           646.549 Joules  |
	-------------------------------------------------

	============================= End of E-SMI ============================

2.	e_smi_library/b$ sudo ./e_smi_tool --showcoreenergy 12 --showsockpower --setpowerlimit 1 220000 --showsockpower

	============================= E-SMI ===================================

	-------------------------------------------------
	| core[012] energy  |            73.467 Joules  |
	-------------------------------------------------

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 174.051          | 169.451          |
	| PowerLimit (Watts)             | 400.000          | 220.000          |
	| PowerLimitMax (Watts)          | 400.000          | 320.000          |
	------------------------------------------------------------------------

	Socket[1] power_limit set to 220.000 Watts successfully

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 174.085          | 169.431          |
	| PowerLimit (Watts)             | 400.000          | 220.000          |
	| PowerLimitMax (Watts)          | 400.000          | 320.000          |
	------------------------------------------------------------------------

3. 	e_smi_library/b$$ ./e_smi_tool --showxgmibandwidth G2 AGG_BW

	============================= E-SMI ===================================


	-------------------------------------------------------------
	| Current Aggregate bandwidth of xGMI link G2 |     40 Mbps |
	-------------------------------------------------------------

	============================= End of E-SMI ============================

4.	e_smi_library/b$sudo ./e_smi_tool --setdfpstaterange 0 1 2
	[sudo] password for user:

	============================= E-SMI ===================================

	Data Fabric PState range(max:1 min:2) set successfully

	============================= End of E-SMI ============================

```

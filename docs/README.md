
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

**Building the library and tool using clang compiler**
* `$ cmake -DUSE_CLANG=1 ../`
* `$ make`


 The built library `libe_smi64_static.a`, `libe_smi64.so.X.Y` and `esmi_tool` will appear in the `build` directory

* `$ sudo make install`

 By default library file, header and tool are installed at /opt/e-sms.
 To change the default installation path, build with cmake -DCMAKE_INSTALL_PREFIX=xxxx.
 Library will be installed at xxxx/lib, tool will be installed at xxxx/bin, header will be installed at xxxx/include and doc will be installed at xxxx/e-sms/doc.
 Example: If -DCMAKE_INSTALL_PREFIX=/usr/local then esmi lib, esmi_tool binary and headers are installed at /usr/local/lib, /usr/local/bin, /usr/local/include respectively.

`Note:`
 Library is dependent on amd_hsmp.h header and without this, compilation will break. Please follow the instruction in "Kernel dependencies" section

## Building the Documentation
The documentation PDF file can be built with the following steps (continued from the steps above)
`$ make doc`
Upon a successful build, the `ESMI_Manual.pdf` and `ESMI_IB_Release_Notes.pdf` will be copied to the top directory of the source.

## Building the package
The RPM and DEB packages can be created with the following steps (continued from the steps above):
`$ make package`

# Kernel version dependency
* Family 0x19 model 00-0fh a0-afh are supported from v5.16-rc7 onwards
* Family 0x19 model 90-9fh are supported from v6.6-rc1 onwards
* Family 0x1A model 00-1fh are supported from v6.5-rc5 onwards

# Kernel driver dependencies
The E-SMI Library depends on the following device drivers from Linux to manage the system management features.

## amd_hsmp driver
This is used to monitor and manage power metrics, boostlimits and other system management features.
The power metrics, boostlimits and other features are managed by the SMU(System Management Unit of the processor) firmware and exposed via PCI config space and accessed through "Host System Management Port(HSMP)" at host/cpu side. AMD provides Linux kernel module(amd_hsmp) exposing this information to the user-space via ioctl interface.
* amd_hsmp driver is accepted in upstream kernel and is available at linux tree at drivers/platform/x86/amd/hsmp.c from version 5.17.rc1 onwards
  either it can be compiled as part of kernel as a module or built in driver or as an out of tree module which is available at https://github.com/amd/amd_hsmp.git
* E-smi compilation has dependency on amd_hsmp header file from uapi header of amd_hsmp driver.
  It should be available at
  * /usr/include/asm/ on RHEL systems
  * /usr/include/x86_64-linux-gnu/asm/ on Ubuntu systems.
  If its not present, it can be copied from [amd_hsmp](https://github.com/amd/amd_hsmp.git) github repo or from the kernel source arch/x86/include/uapi/asm/amd_hsmp.h
* There is always a dependency between E-smi and amd_hsmp driver versions.
  The new features of E-smi work only if there is a matching HSMP driver.
## amd_hsmp/msr_safe/amd_energy/msr
One of these drivers is needed to monitor energy counters.
* AMD family 19h, model 00-0fh and 30-3fh
  * These processors support energy monitoring through 32 bit RAPL MSR registers.
  * amd_energy driver, an out of tree kernel module, hosted at [amd_energy](https://github.com/amd/amd_energy) can report per core and per socket counters via the HWMON sysfs entries.
  * This driver provides accumulation of energy for avoiding wrap around problem.
  * This is the only supported energy driver for 32bit RAPLS
* AMD family 19h, model 10-1fh, a0-afh and 90-9fh, AMD family 0x1A, model 00-1fh
  * These processors support energy monitoring through 64 bit RAPL MSR registers.
  * Because of 64 bit registers, there is no accumulation of energy needed.
  * For these processors either  [msr_safe](https://github.com/LLNL/msr-safe.git),  [amd_energy](https://github.com/amd/amd_energy) or kernel's default msr driver can be used.
* AMD family 1Ah, model 0x00-0x1f support RAPL reading using HSMP mailbox.
  * For these processors either amd_hsmp driver or msr_safe driver or amd_energy driver or msr driver can be used.

  * The order of checking for the availability of drivers in e-smi is as follows.
    * If amd_hsmp driver is present and supports RAPL reading, this is used for reading energy.
    * If amd_hsmp driver is not present/not supports energy reading, and msr-safe driver is present, this is used for reading energy.
      Msr-safe driver needs allowlist file to be written to "/dev/cpu/msr_allowlist" for allowing the read of those specific msr registers.
      Please follow below steps or use the tool option "writemsrallowlist" to write the allowlist file.
      Create "amd_allowlist" file with below contents and run the command "sudo su" and "cat amd_allowlist > /dev/cpu/msr_allowlist".
      * 0xC0010299 0x0000000000000000 # "ENERGY_PWR_UNIT_MSR"
      * 0xC001029A 0x0000000000000000 # "ENERGY_CORE_MSR"
      * 0xC001029B 0x0000000000000000 # "ENERGY_PKG_MSR"
      * Note: The first column above indicates MSR register address and 2nd column indicates write mask and the third coulmn is name of the register.
    * If msr_safe driver is not present, amd_energy driver is present, this is used for reading energy.
    * If msr_safe driver or amd_energy driver not present, msr driver will be used for reading energy.
    * Any one of msr_safe/amd_energy/msr driver is sufficient

# BIOS dependency
* To get HSMP working. PCIe interface needs to be enabled in the BIOS. On the reference BIOS please follow the sequence below for enabling HSMP.

  **Advanced > AMD CBS > NBIO Common Options > SMU Common Options > HSMP Support**

  **BIOS Default:     “Auto” (Disabled)**
		to
  **BIOS Default:     "Enabled"**

  If the above HSMP support option is disabled, the related E-SMI APIs will return -ETIMEDOUT.
  The latest BIOS supports probing of HSMP driver through ACPI device.
  The ACPI supported [amd_hsmp](https://github.com/amd/amd_hsmp.git) driver version is 2.2

# Supported hardware
* AMD Zen3 based CPU Family `19h` Models `0h-Fh` and `30h-3Fh`.
* AMD Zen4 based CPU Family `19h` Models `10h-1Fh` and `A0-AFh`.
* AMD Zen4 based CPU Family `19h` Models `90-9Fh`.
* AMD Zen5 based CPU Family `1Ah` Models `00-1Fh`.
* AMD Zen6 based CPU Family `1Ah` Models `50-5Fh`.

# Additional required software for building
In order to build the E-SMI library, the following components are required. Note that the software versions listed are what is being used in development. Earlier versions are not guaranteed to work:
* CMake (v3.5.0)
* gcc, g++, make
* build-essential

In order to build the latest documentation, the following are required:

* DOxygen (1.8.13)
* latex (pdfTeX 3.14159265-2.6-1.40.18)

# Library Usage Basics
Many of the functions in the library take a "core/socket index". The core/socket index is a number greater than or equal to 0, and less than the number of cores/sockets on the system. Number of cores/sockets in a system can be obtained from esmi library APIs.

## Hello E-SMI
The only required E-SMI call for any program that wants to use E-SMI is the `esmi_init()` call. This call initializes some internal data structures that will be used by subsequent E-SMI calls.

When E-SMI is no longer being used, `esmi_exit()` should be called. This provides a way to do any releasing of resources that E-SMI may have held. In many cases, this may have no effect, but may be necessary in future versions of the library.

Below is a simple "Hello World" type program that display the Average Power of Sockets.

```c
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

	total_sockets = esmi_number_of_sockets_get();
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

	============================= E-SMI ===================================

	--------------------------------------
	| CPU Family            | 0x1a (26 ) |
	| CPU Model             | 0x50 (80 ) |
	| NR_CPUS               | 1024       |
	| NR_SOCKETS            | 2          |
	| THREADS PER CORE      | 2 (SMT ON) |
	--------------------------------------

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Energy (K Joules)              | 2707.379         | 2799.587         |
	| Power (Watts)                  | 192.401          | 212.680          |
	| PowerLimit (Watts)             | 400.000          | 400.000          |
	| PowerLimitMax (Watts)          | 600.000          | 600.000          |
	| C0 Residency (%)               | 0                | 0                |
	| DDR Bandwidth                  |                  |                  |
	|       DDR Max BW (GB/s)        | 1024             | 1024             |
	|       DDR Utilized BW (GB/s)   | 0                | 0                |
	|       DDR Utilized Percent(%)  | 0                | 0                |
	| Current Active Freq limit      |                  |                  |
	|        Freq limit (MHz)        | 600              | 4000             |
	|        Freq limit source       | Refer below[*0]  | Refer below[*1]  |
	| Socket frequency range         |                  |                  |
	|        Fmax (MHz)              | 4000             | 4000             |
	|        Fmin (MHz)              | 600              | 600              |
	------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU Energies in Joules:                                                                                       |
	| cpu [  0] :    715.115    389.712    377.251    402.663    392.709    386.526    362.794    398.745           |
	| cpu [  8] :    386.852    383.468    273.061    396.164    392.228    379.505    274.754    394.944           |
	| cpu [ 16] :    388.046    383.407    271.922    394.308    390.007    379.800    273.709    391.307           |
	| cpu [ 24] :    404.950    375.612    270.050    387.430    388.139    375.280    270.646    385.815           |
	| cpu [ 32] :    385.557    387.318    276.909    395.703    382.611    383.575    269.570    388.475           |
	| cpu [ 40] :    382.775    379.642    261.102    386.158    386.012    381.224    260.544    384.005           |
	| cpu [ 48] :    384.191    381.496    263.967    381.579    382.114    389.644    264.013    383.515           |
	| cpu [ 56] :    410.096    374.925    262.407    380.127    378.513    377.513    263.375    380.687           |
	| cpu [ 64] :    370.425    371.524    351.906    375.294    363.956    365.371    350.187    371.524           |
	| cpu [ 72] :    362.701    366.613    263.898    364.786    363.998    369.227    264.627    367.206           |
	| cpu [ 80] :    367.419    367.770    255.007    367.077    363.071    369.155    251.745    365.665           |
	| cpu [ 88] :    373.240    366.848    276.361    359.818    361.454    363.394    268.540    362.873           |
	| cpu [ 96] :    358.900    367.274    359.404    372.265    355.492    364.660    358.123    375.146           |
	| cpu [104] :    359.528    361.320    351.466    372.051    356.591    362.857    354.927    371.970           |
	| cpu [112] :    357.492    361.706    351.899    368.802    363.282    361.058    353.488    365.508           |
	| cpu [120] :    358.423    362.262    356.772    369.325    357.314    357.966    351.722    372.592           |
	| cpu [128] :    356.002    354.649    303.472    360.880    358.561    355.623    289.281    357.498           |
	| cpu [136] :    358.479    357.664    244.267    357.757    356.329    354.793    242.702    358.781           |
	| cpu [144] :    356.358    357.908    259.599    356.907    356.961    353.358    243.875    356.191           |
	| cpu [152] :    370.226    351.249    244.479    357.326    354.730    353.162    241.768    355.618           |
	| cpu [160] :    361.206    353.729    350.767    360.348    358.435    351.262    351.394    359.950           |
	| cpu [168] :    357.600    353.224    254.072    359.746    357.761    350.796    249.407    356.764           |
	| cpu [176] :    359.527    350.354    246.302    354.140    358.747    348.206    246.495    355.992           |
	| cpu [184] :    366.479    350.525    317.251    354.258    355.557    348.948    252.982    357.158           |
	| cpu [192] :    409.289    389.502    391.695    408.610    429.364    388.688    388.983    405.432           |
	| cpu [200] :    406.562    389.359    387.329    405.920    409.421    383.792    389.652    406.717           |
	| cpu [208] :    410.076    385.989    385.324    400.622    405.065    382.874    383.948    402.007           |
	| cpu [216] :    409.791    381.028    384.452    396.890    407.245    384.938    389.196    398.668           |
	| cpu [224] :    380.330    388.242    376.741    387.324    378.768    387.299    374.296    389.518           |
	| cpu [232] :    381.405    387.582    279.803    385.738    385.669    388.777    275.335    383.606           |
	| cpu [240] :    383.475    389.353    274.885    382.300    383.295    383.827    275.878    380.710           |
	| cpu [248] :    383.397    388.116    277.176    372.159    381.769    386.866    272.554    373.093           |
	| cpu [256] :    393.333    276.968    278.731    279.435    279.422    275.773    280.563    278.147           |
	| cpu [264] :    275.768    275.205    264.025    276.483    275.595    273.323    271.650    275.396           |
	| cpu [272] :    275.910    272.999    274.969    272.700    274.645    273.506    270.490    273.544           |
	| cpu [280] :    273.217    265.141    272.112    241.626    274.747    275.004    274.761    231.485           |
	| cpu [288] :    284.285    285.515    284.499    284.210    284.129    287.391    284.950    286.414           |
	| cpu [296] :    282.546    286.729    279.075    283.385    281.980    286.662    279.306    282.824           |
	| cpu [304] :    283.969    286.026    281.449    281.923    283.906    287.221    279.284    282.056           |
	| cpu [312] :    285.801    285.964    280.330    280.140    283.019    286.900    279.729    281.334           |
	| cpu [320] :    386.492    365.793    274.902    383.504    376.639    286.289    275.301    380.110           |
	| cpu [328] :    363.156    290.053    273.119    283.064    315.190    282.917    274.090    285.763           |
	| cpu [336] :    340.234    281.506    272.681    282.912    314.930    283.143    274.471    280.249           |
	| cpu [344] :    387.147    282.747    273.778    279.845    304.219    287.594    274.308    282.882           |
	| cpu [352] :    286.039    298.821    275.821    281.052    317.523    281.581    275.883    281.739           |
	| cpu [360] :    292.960    283.028    275.383    302.654    311.094    282.117    274.814    284.592           |
	| cpu [368] :    291.132    282.189    276.568    284.658    283.117    278.674    274.079    280.162           |
	| cpu [376] :    385.395    280.294    274.904    282.020    284.450    282.847    275.639    280.564           |
	| cpu [384] :    285.262    275.056    234.562    279.096    279.178    274.413    222.827    278.619           |
	| cpu [392] :    278.362    273.612    163.878    275.221    280.125    274.381    164.827    277.481           |
	| cpu [400] :    277.735    272.190    165.691    274.116    279.448    272.696    165.114    274.598           |
	| cpu [408] :    283.645    272.231    165.549    272.455    277.979    272.352    163.768    271.963           |
	| cpu [416] :    275.511    272.776    271.425    274.759    276.275    273.723    270.303    274.152           |
	| cpu [424] :    276.007    273.363    268.273    273.725    276.496    274.137    245.687    271.894           |
	| cpu [432] :    277.033    273.048    268.134    272.523    276.575    273.113    269.287    272.929           |
	| cpu [440] :    275.709    272.688    268.463    271.633    276.629    271.799    272.414    275.263           |
	| cpu [448] :    286.123    274.400    275.145    277.166    282.164    273.073    274.365    277.252           |
	| cpu [456] :    279.842    277.289    265.581    276.250    279.976    277.285    182.949    275.482           |
	| cpu [464] :    283.951    278.163    239.741    276.345    279.564    279.341    227.606    276.804           |
	| cpu [472] :    283.708    275.600    173.280    268.046    278.184    276.693    234.639    235.688           |
	| cpu [480] :    279.618    279.069    278.126    280.123    279.460    281.294    276.406    280.107           |
	| cpu [488] :    279.872    280.813    277.594    279.799    277.946    280.552    274.407    278.422           |
	| cpu [496] :    281.525    279.806    275.308    278.712    280.750    279.171    274.860    278.391           |
	| cpu [504] :    281.392    278.454    274.722    275.910    282.531    279.507    276.171    279.015           |
	-----------------------------------------------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU BoostLimit in MHz:                                                                                        |
	| cpu [  0] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [ 16] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [ 32] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [ 48] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [ 64] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [ 80] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [ 96] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [112] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [128] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [144] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [160] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [176] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [192] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [208] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [224] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [240] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [256] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [272] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [288] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [304] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [320] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [336] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [352] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [368] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [384] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [400] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [416] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [432] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [448] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [464] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [480] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	| cpu [496] : 4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000  4000    |
	-----------------------------------------------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU FloorLimit in MHz:                                                                                        |
	| cpu [  0] : 3200  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 16] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 32] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 48] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 64] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 80] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 96] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [112] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [128] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [144] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [160] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [176] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [192] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [208] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [224] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [240] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [256] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [272] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [288] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [304] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [320] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [336] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [352] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [368] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [384] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [400] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [416] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [432] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [448] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [464] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [480] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	| cpu [496] : 0     0     0     0     0     0     0     0     0     0     0     0     0     0     0     0       |
	-----------------------------------------------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU EffectiveFloorLimit in MHz:                                                                               |
	| cpu [  0] : 3200  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 16] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 32] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 48] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 64] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 80] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [ 96] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [112] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [128] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [144] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [160] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [176] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [192] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [208] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [224] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [240] : 2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500  2500    |
	| cpu [256] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [272] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [288] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [304] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [320] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [336] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [352] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [368] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [384] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [400] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [416] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [432] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [448] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [464] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [480] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [496] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	-----------------------------------------------------------------------------------------------------------------

	-----------------------------------------------------------------------------------------------------------------
	| CPU core clock current frequency limit in MHz:                                                                |
	| cpu [  0] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [ 16] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [ 32] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [ 48] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [ 64] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [ 80] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [ 96] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [112] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [128] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [144] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [160] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [176] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [192] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [208] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [224] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [240] : 600   600   600   600   600   600   600   600   600   600   600   600   600   600   600   600     |
	| cpu [256] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [272] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [288] : 1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  2781    |
	| cpu [304] : 1791  1791  1791  4000  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791  1791    |
	| cpu [320] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [336] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [352] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [368] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [384] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [400] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [416] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [432] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [448] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [464] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [480] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	| cpu [496] : 1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800  1800    |
	-----------------------------------------------------------------------------------------------------------------
	*0 Frequency limit source names:
	 OPN Max

	*1 Frequency limit source names:
	 OPN Max


	Try `./e_smi_tool --help' for more information.

```

For detailed and up to date usage information, we recommend consulting the help:

For convenience purposes, following is the output from the -h flag on hsmp protocol version 7 based system:

```

============================= E-SMI ===================================

Usage: ./e_smi_tool [Option]... <INPUT>...
Help : ./e_smi_tool --help

Output Option<s>:
  -h, --help                                                    Show this help message
  -A, --showall                                                 Show all esmi parameter values
  -V  --version                                                 Show e-smi library version
  --testmailbox [SOCKET] [VALUE<0-0xFFFFFFFF>]                  Test HSMP mailbox interface
  --writemsrallowlist                                           Write msr-safe allowlist file
  --json                                                        Print output on console as json format[applicable only for get commands]
  --csv                                                         Print output on console as csv format[applicable only for get commands]
  --initialdelay [INITIAL_DELAY] <TIME_RANGE<ms,s,m,h,d>>       Initial delay before start of execution
  --loopdelay    [LOOP_DELAY]    <TIME_RANGE<ms,s,m,h,d>>       Loop delay before executing each loop
  --loopcount    [LOOP_COUNT]                                   Set the loop count to the specified value[pass "-1" for infinite loop]
  --stoploop     [STOPLOOP_FILE_NAME]                           Set the StopLoop file name, loop will stop once the stoploop file is available
  --printonconsole [ENABLE_PRINT<0-1>]                          Print output on console if set to 1, or 0 to suppress the console output
  --log [LOG_FILE_NAME]                                         Set the Log file name, in which the data collected need to be logged

Get Option<s>:
  --showcoreenergy [CORE]                                       Show energy for a given CPU (Joules)
  --showsockenergy                                              Show energy for all sockets (KJoules)
  --showsockpower                                               Show power metrics for all sockets (Watts)
  --showcorebl [CORE]                                           Show BoostLimit for a given CPU (MHz)
  --showsockc0res [SOCKET]                                      Show C0Residency for a given socket (%)
  --showsmufwver                                                Show SMU FW Version
  --showhsmpdriverver                                           Show HSMP Driver Version
  --showhsmpprotover                                            Show HSMP Protocol Version
  --showprochotstatus                                           Show HSMP PROCHOT status for all sockets
  --showclocks                                                  Show Clock Metrics (MHz) for all sockets
  --showddrbw                                                   Show DDR bandwidth details (Gbps)
  --showdimmtemprange [SOCKET] [DIMM_ADDR]                      Show dimm temperature range and refresh rate for a given socket and dimm address
  --showdimmthermal [SOCKET] [DIMM_ADDR]                        Show dimm thermal values for a given socket and dimm address
  --showdimmpower [SOCKET] [DIMM_ADDR]                          Show dimm power consumption for a given socket and dimm address
  --showcclkfreqlimit [CORE]                                    Show current clock frequency limit(MHz) for a given core
  --showsvipower                                                Show svi based power telemetry of all rails for all sockets
  --showiobw [SOCKET] [LINK<P0-P2,P4-P5,G0-G2>]                 Show IO aggregate bandwidth for a given socket and linkname
  --showlclkdpmlevel [SOCKET] [NBIOID<0-3>]                     Show lclk dpm level for a given nbio in a given socket
  --showsockclkfreqlimit [SOCKET]                               Show current clock frequency limit(MHz) for a given socket
  --showxgmibw [SOCKET] [LINK<P0-P2,G0-G2>] [BW<AGG_BW,RD_BW,WR_BW>]
                                                                Show xGMI bandwidth for a given socket, linkname and bwtype
  --showcurrpwrefficiencymode [SOCKET]                          Show current power effciency mode
  --showcpurailisofreqpolicy [SOCKET]                           Show current CPU ISO frequency policy
  --showdfcstatectrl [SOCKET]                                   Show current DF C-state status
  --getapbstatus [SOCKET]                                       Get APB status and Data Fabric pstate(if APBDisabled)
  --getxgmiwidth [SOCKET]                                       Get xgmi link width
  --getdfpstaterange [SOCKET]                                   Get df pstate range for a given socket
  --getxgmipstaterange [SOCKET]                                 Get xgmi pstate range for a given socket
  --getccdpower [CORE]                                          Get CCD power for a given core
  --gettdelta [SOCKET]                                          Get thermal solution behaviour for a given socket
  --getsvi3vrtemp [SOCKET] [TYPE] [RAIL_INDEX(if TYPE=1)]       Get svi3 vr controller temperature(TYPE:0->HottestRail,1->IndividualRail)
  --getdimmsbdata [SOCKET] [DIMM_ADDR] [LID] [OFFSET] [REGSPACE]
                                                                Get DIMM SB register data
                                                                (LID:0x2->TS0,0x6->TS1,0xA->SPDHub)(REGSPACE:0->Volatile,1->NVM)
  --getpc6enable [SOCKET]                                       Get the PC6 Enable Control
  --getcc6enable [SOCKET]                                       Get the CC6 Enable Control
  --getenabledcommands [SOCKET]                                 Get the HSMP Enabled Commands
  --getcorefl [CORE]                                            Get FloorLimit for a given CPU/LogicalCore (MHz)
  --getcoreefffl [CORE]                                         Get EffectiveFloorLimit for a given CPU/LogicalCore (MHz)
  --getsdpslimit [SOCKET]                                       Get the SDPS Limit for a given socket (Watts)

Set Option<s>:
  --setpowerlimit [SOCKET] [POWER]                              Set power limit for a given socket (mWatts)
  --setcorebl [CORE] [BOOSTLIMIT]                               Set BoostLimit for a given core (MHz)
  --setsockbl [SOCKET] [BOOSTLIMIT]                             Set BoostLimit for a given Socket (MHz)
  --apbdisable [SOCKET] [PSTATE<0-2>]                           Set Data Fabric Pstate for a given socket
  --apbenable [SOCKET]                                          Enable the Data Fabric performance boost algorithm for a given socket
  --setxgmiwidth [SOCKET] [MIN<0-2>] [MAX<0-2>]                 Set xgmi link width in a multi socket system (MAX >= MIN)
  --setlclkdpmlevel [SOCKET] [NBIOID<0-3>] [MIN<0-3>] [MAX<0-3>]Set lclk dpm level for a given nbio in a given socket (MAX >= MIN)
  --setdfpstaterange [SOCKET] [MIN<0-2>] [MAX<0-2>]             Set df pstate range for a given socket (MAX <= MIN)
  --setpowerefficiencymode [SOCKET] [MODE<0-5>] [UTIL<0-100>(If MODE=4/5)] [PPTLimit(mW)(If MODE=4/5)]
                                                                Set power efficiency mode for a given socket.
                                                                If Mode=4/5, UTIL(%)<0-100> and PPTLimit(mW) are mandatory
                                                                        0=HighPerformance,
                                                                        1=PowerEfficiency,
                                                                        2=IOPerformance,
                                                                        3=BalancedMemory,
                                                                        4=BalancedCore,
                                                                        5=BalancedCoreMemory
  --setxgmipstaterange [SOCKET] [MIN<0,1>] [MAX<0,1>]           Set xgmi pstate range
  --setcpurailisofreqpolicy [SOCKET] [VAL<0,1>]                 Set CPU ISO frequency policy
  --setdfcctrl [SOCKET] [VAL<0,1>]                              Enable or disable DF c-state
  --setdimmsbdata [SOCKET] [DIMM_ADDR] [LID] [OFFSET] [REGSPACE] [REGDATA]
                                                                Set DIMM SB register data
                                                                (LID:0x2->TS0,0x6->TS1,0xA->SPDHub)(REGSPACE:0->Volatile,1->NVM)
  --setpc6enable [SOCKET] [val<0,1>]                            Set the PC6 Enable Control
  --setcc6enable [SOCKET] [val<0,1>]                            Set the CC6 Enable Control
  --setcorefl [CORE] [FLOORLIMIT]                               Set FloorLimit for a given core (MHz)
  --setsockfl [SOCKET] [FLOORLIMIT]                             Set FloorLimit for a given Socket (MHz)
  --setmsrcorefl [CORE] [FLOORLIMIT]                            Set FloorLimit for a given core (MHz) through MSR
  --setmsrsockfl [SOCKET] [FLOORLIMIT]                          Set FloorLimit for a given Socket (MHz) through MSR
  --setsdpslimit [SOCKET] [SDPSLIMIT]                           Set SDPS Limit for a given Socket (mWatts)

============================= End of E-SMI ============================


```
Following are the value ranges and other information needed for passing it to tool
```
1.	----showxgmibw [SOCKET] [LINKNAME] [BWTYPE]

	  LINKNAME :
	  Rolling Stones:P0/P1/P2/P3/G0/G1/G2/G3
	  MI300:G0/G1/G2/G3/G4/G5/G6/G7
	  Family 0x1A, model 0x00-0x1F:P1/P3/G0/G1/G2/G3
	  Family 0x1A, model 0x50-0x5F:P0/P1/P2/G0/G1/G2

	  BWTYPE : AGG_BW/RD_BW/WR_BW

2.	--setxgmiwidth [MIN] [MAX]

	  MIN : MAX :  0 - 2 with MIN <= MAX

3.	--showlclkdpmlevel [SOCKET] [NBIOID]

	  NBIOID : 0 - 3

4.	--apbdisable [SOCKET] [PSTATE]

	  PSTATE : 0 - 2

5.	--setlclkdpmlevel [SOCKET] [NBIOID] [MIN] [MAX]

	  NBIOID : 0 - 3

	  MI300A:  MIN : MAX : 0 - 2 with MIN <= MAX
	  Other platforms: MIN : MAX : 0 - 3 with MIN <= MAX

6. 	--setpcielinkratecontrol [SOCKET] [CTL]

	  CTL : 0 - 2

7.	--setpowerefficiencymode [SOCKET] [MODE]

	  Rolling Stones: MODE : 0 - 3
	  Family 0x1A model 0x00-0x1F or 0x50-0x5F: MODE : 0 - 5

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

5.	e_smi_library/b$./e_smi_tool --showsockpower --showprochotstatus --loopdelay 1 s --loopcount 10 --log power.csv --printonconsole 0

	e_smi_library/b$cat power.csv
	2025-06-10,12:39:37:88,45.951,43.225,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:38:98,39.572,39.175,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:39:105,39.539,38.884,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:40:117,41.892,42.220,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:41:123,40.466,39.659,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:42:134,39.681,39.218,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:43:138,39.349,38.562,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:44:145,39.517,38.807,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:45:148,39.726,39.457,400.000,400.000,500.000,500.000,inactive,inactive,
	2025-06-10,12:39:46:160,39.393,38.699,400.000,400.000,500.000,500.000,inactive,inactive,

6.	e_smi_library/b$./e_smi_tool --showsockc0res 0 --showcorebl 0 --showsockc0res 1 --json
	{
    	    {
                "Socket":0,
                "C0Residency(%)":0
            },
            {
                "Core":0,
                "BoostLimit(MHz)":4100
            },
            {
                "Socket":1,
                "C0Residency(%)":0
            },
            {
                "JSONFormatVersion":1
            }
	}

7.	e_smi_library/b$./e_smi_tool --showsockc0res 0 --showcorebl 0 --showsockc0res 1 --csv
	Socket,C0Residency(%)
	0,0
	Core,BoostLimit(MHz)
	0,4100
	Socket,C0Residency(%)
	1,0

8.	//To display the data in the console for a specified number of user-defined loops and loop delay.
	e_smi_library/b$./e_smi_tool --showsockpower --initialdelay 2 s --loopdelay 1 s --loopcount 2

	============================= E-SMI ===================================


	* InitialDelay(in secs):2.000000, ...

	* LoopCount:0, LoopDelay(in secs):1.000000, ...
	* CurrentTime:2025-06-13,11:40:18:367

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 48.148           | 42.990           |
	| PowerLimit (Watts)             | 400.000          | 400.000          |
	| PowerLimitMax (Watts)          | 500.000          | 500.000          |
	------------------------------------------------------------------------


	* LoopCount:1, LoopDelay(in secs):1.000000, ...
	* CurrentTime:2025-06-13,11:40:19:370

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 103.711          | 87.128           |
	| PowerLimit (Watts)             | 400.000          | 400.000          |
	| PowerLimitMax (Watts)          | 500.000          | 500.000          |
	------------------------------------------------------------------------

9.	//To output the data to the console and simultaneously record it in log(CSV format) for user-defined loops and loop delay.
	e_smi_library/b$./e_smi_tool --showsockpower --initialdelay 2 s --loopdelay 1 s --loopcount 2 --log power.csv

	============================= E-SMI ===================================


	* InitialDelay(in secs):2.000000, ...

	* LoopCount:0, LoopDelay(in secs):1.000000, ...
	* CurrentTime:2025-06-13,11:40:18:367

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 48.148           | 42.990           |
	| PowerLimit (Watts)             | 400.000          | 400.000          |
	| PowerLimitMax (Watts)          | 500.000          | 500.000          |
	------------------------------------------------------------------------


	* LoopCount:1, LoopDelay(in secs):1.000000, ...
	* CurrentTime:2025-06-13,11:40:19:370

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 103.711          | 87.128           |
	| PowerLimit (Watts)             | 400.000          | 400.000          |
	| PowerLimitMax (Watts)          | 500.000          | 500.000          |
	------------------------------------------------------------------------

	e_smi_library/b$cat power.csv
	Date,Timestamp,Socket0:Power(Watts),Socket1:Power(Watts),Socket0:PowerLimit(Watts),Socket1:PowerLimit(Watts),Socket0:PowerLimitMax(Watts),Socket1:PowerLimitMax(Watts),
	2025-06-13,11:43:22:587,41.007,39.949,400.000,400.000,500.000,500.000,
	2025-06-13,11:43:23:590,47.329,46.269,400.000,400.000,500.000,500.000,

10.	//To continuously collect data in the log(CSV format) without interruption until the exit condition is met(such as detecting a stoploop file).
	[Terminal 1]:
	  e_smi_library/b$./e_smi_tool --showsockpower --loopdelay 1 s --loopcount -1 --log power.csv --printonconsole 0 --stoploop stresslog.txt

	[Terminal 2]:
	  //Consider the user initiates a stress test for a random duration, which generates a stresslog.txt file upon completion of stress test.
	  //For experimental purposes, we manually generate the stresslog.txt file after a random duration.
	  e_smi_library/b$touch stresslog.txt

	[Terminal 1]:
	  //The execution of e_smi_tool should have concluded by now, allowing the user to examine power.csv file generated during the stress test.
	  e_smi_library/b$cat power.csv
	  Date,Timestamp,Socket0:Power(Watts),Socket1:Power(Watts),Socket0:PowerLimit(Watts),Socket1:PowerLimit(Watts),Socket0:PowerLimitMax(Watts),Socket1:PowerLimitMax(Watts),
	  2025-06-13,11:53:53:245,40.082,39.281,400.000,400.000,500.000,500.000,
	  2025-06-13,11:53:54:247,40.521,39.533,400.000,400.000,500.000,500.000,
	  2025-06-13,11:53:55:249,42.126,40.270,400.000,400.000,500.000,500.000,
	  2025-06-13,11:53:56:253,42.416,40.861,400.000,400.000,500.000,500.000,
	  2025-06-13,11:53:57:254,41.132,40.421,400.000,400.000,500.000,500.000,
	  2025-06-13,11:53:58:258,40.363,39.443,400.000,400.000,500.000,500.000,
	  2025-06-13,11:53:59:265,41.472,40.382,400.000,400.000,500.000,500.000,

11.	//To continuously display the data in the console for an indefinite duration (press CTRL+C to stop).
	e_smi_library/b$./e_smi_tool --showsockpower --loopdelay 1 s --loopcount -1

	============================= E-SMI ===================================


	* LoopCount:0, LoopDelay(in secs):1.000000, ...
	* CurrentTime:2025-06-13,12:10:08:350

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 43.373           | 40.041           |
	| PowerLimit (Watts)             | 400.000          | 400.000          |
	| PowerLimitMax (Watts)          | 500.000          | 500.000          |
	------------------------------------------------------------------------


	* LoopCount:1, LoopDelay(in secs):1.000000, ...
	* CurrentTime:2025-06-13,12:10:09:353

	------------------------------------------------------------------------
	| Sensor Name                    | Socket 0         | Socket 1         |
	------------------------------------------------------------------------
	| Power (Watts)                  | 40.273           | 39.875           |
	| PowerLimit (Watts)             | 400.000          | 400.000          |
	| PowerLimitMax (Watts)          | 500.000          | 500.000          |
	------------------------------------------------------------------------


	* LoopCount:2, LoopDelay(in secs):1.000000, ...
	^C

```

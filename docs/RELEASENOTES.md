
# EPYC™ System Management Interface (E-SMI) In-band Library

NEW! E-SMI library beta 4.1.2 is now available

The EPYC™ System Management Interface In-band Library, or E-SMI library, is a C library for Linux that provides a user space interface to monitor and control the CPU's power, energy, performance and other system management features.

# Changes Notes

## Highlights of release v4.1.2
* Bug fix release

## Highlights of release v4.1.1
* Clang compiler is supported.
* Some of the bugs are fixed.
* tool option is added to read the HSMP driver version.

## Highlights of release v4.0.0
* AMD Family 0x1A and model 0x00-0x1f processors are supported in this release.
* Any of the hsmp/amd_energy/msr_safe/msr driver can be used to monitor energy.

## Highlights of release v3.0.0
* AMD MI300 processors are supported in this release.
* Library is modified to support platform specific check in each message in an organised way.
* tool options are modified to show valid input values

## Highlights of minor release v2.1
* Library is updated to align with changes in the processor spec

## Highlights of release v2.0

* Supports new HSMP protocol version 5 messages, defined for Family 19h Model 10h - SP5
    * New APIs are added for platform features
    * esmi_tool is update with platform specific features


## Highlights of release v1.5

* Supports ioctl based implementation of hsmp driver
  with support for follwoing new APIs
    * Set XGMI link width for 2P connected systems
    * Set LCLK dpm level for NBIO id
    * APB Disable and Enable messages

## Highlights of minor release v1.2

* Support to compile ESMI In-band library as static
* Support for new system management features in tool and library, such as
    * Get SMU Firmware version
    * Get PROCHOT status
    * Get clocks
        * CPU clock frequency limit
        * Data Fabric Clock(fclk),
        * DRAM Memory Clock(mclk) and
    * Provide maximum DDR bandwidth(theoritical) & DDR bandwidth utilization
* Add more options and improve tool's console output for readability

## Highlights of minor release v1.1

* Support for creating RPM and DEB packages
* Auxiliary APIs to provide system topology
* An API to read all the Energy counters on the CPU at once.
* Single command to create doxygen based PDF document
* Updated e_smi_tool supporting all the above information
* Cosmetic changes to the tool

## Highlights of major release v1.0

* Power
    * Current Power Consumed
    * Power Limit
    * Max Power Limit
* Performance
    * Boostlimit
* Energy
    * Energy Consumed
* e_smi_tool, user application supporting all the above information.

# Specifications

## Processors:
Target released for AMD EPYC™ processor Family 19h, model 0h-1Fh, 30h-3Fh, 90h-9Fh, A0h-AFh and Family 0x1A model 0h-1Fh.

## Operating Systems
AMD ESMI In-band library is tested on following distributions
* Ubuntu 18.04, 20.04
* SUSE SLES 15 and
* RHEL 8.1

# Dependency
This new e-smi release works well with [amd_hsmp](https://github.com/amd/amd_hsmp.git) driver version 2.4.
Not all features will work with version < 2.4. Setting cpu rail iso frequency policy, df c-state enabling,
xGMI pstate range setting etc will only work with 2.4 version of amd_hsmp driver.

# Resources and Technical Support
## Resources
* Documentation:
	https://github.com/amd/esmi_ib_library/blob/master/ESMI_Manual.pdf
* Source code:
	https://github.com/amd/esmi_ib_library

## Support
Thank you for using AMD ESMI In-band Library. Please use [ESMI In-band Support](https://github.com/amd/esmi_ib_library/issues) for bug reports, support and feature requests.

## Known Issues
* In creating package if "make install" is used previously with "sudo", need to create package with sudo permission, "sudo make package", else permission denied error is popped.

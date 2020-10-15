# EPYC™ System Management Interface (E-SMI) In-band Library

Thank you for using AMD ESMI In-band Library. Please use the [ESMI In-band Support](https://github.com/amd/esmi_ib_library/issues) to provide your feedback.

# Changes Notes

## Highlights of minor release v1.1

* Support for creating RPM and DEB packages
* Auxiliary APIs to provide system topology
    * esmi_number_of_cpus_get() to get the core count
    * esmi_number_of_sockets_get() to get socket count
    * esmi_threads_per_core_get() to get threads per core
    * esmi_cpu_family_get() to get the cpu family
    * esmi_cpu_model_get() to get the cpu model
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

# Supported Processors
* Family 17h, model 30 (Rome) and Family 19h, model 0 (Milan).

# Supported Operating Systems
AMD ESMI In-band library supports Linux based following Operating System
* Ubuntu 16.04 & later
* RHEL 7.0 & later
* SLES 15

# System Requirements
ESMI In-band library can be used on any aboved mentioned "Supported Processors" with Dependent drivers installed.

In order to build the E-SMI library, the following components are required. Note that the software versions listed are what is being used in development. Earlier versions are not guaranteed to work:
* CMake (v3.5.0)

In order to build the latest documentation, the following are required:

* Doxygen (1.8.13)
* latex (pdfTeX 3.14159265-2.6-1.40.18)

# Dependencies
The E-SMI Library depends on the following device drivers from Linux to manage the system management features.

## Monitoring Energy counters
The Energy counters are exposed via the RAPL MSRs and the AMD Energy driver exposes the per core and per socket information via the HWMON sys entries. The AMD Energy driver is upstreamed and available as part of Linux v5.8, this driver may be insmoded as a module.

## Monitoring and Managing Power metrics, Boostlimits
The power metrics and Boostlimits features are managed by the SMU firmware and exposed via SMN PCI config space. AMD provided Linux HSMP driver exposes this information to the user-space via sys entries.

# Known Issues
* In creating package if "make install" is used previously with "sudo", need to create package with sudo permission, "sudo make package", else permission denied error is popped.

# Support
Please use [ESMI In-band Support](https://github.com/amd/esmi_ib_library/issues) for bug reports, support and feature requests.

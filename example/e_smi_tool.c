/*
 * University of Illinois/NCSA Open Source License
 *
 * Copyright (c) 2020, Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Developed by:
 *
 *                 AMD Research and AMD Software Development
 *
 *                 Advanced Micro Devices, Inc.
 *
 *                 www.amd.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimers.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimers in
 *    the documentation and/or other materials provided with the distribution.
 *  - Neither the names of <Name of Development Group, Name of Institution>,
 *    nor the names of its contributors may be used to endorse or promote
 *    products derived from this Software without specific prior written
 *    permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS WITH THE SOFTWARE.
 *
 */

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <e_smi/e_smi.h>

#define RED "\x1b[31m"
#define RESET "\x1b[0m"

#define ARGS_MAX 64

void show_smi_parameters(void);
void show_smi_all_parameters(void);
void show_smi_message(void);
void show_smi_end_message(void);

esmi_status_t epyc_get_coreenergy(uint32_t core_id)
{
	esmi_status_t ret;
	uint64_t core_input = 0;

	ret = esmi_core_energy_get(core_id, &core_input);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get core[%d] energy, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("core[%d]/energy\t: %12ld uJoules\n",
		core_id, core_input);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_sockenergy(uint32_t sock_id)
{
	esmi_status_t ret;
	uint64_t pkg_input = 0;

	ret = esmi_socket_energy_get(sock_id, &pkg_input);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] energy, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("socket[%d]/energy\t: %12ld uJoules\n",
		sock_id, pkg_input);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_sockpower(uint32_t sock_id)
{
	esmi_status_t ret;
	uint32_t power = 0;

	/* Get the Average power consumption for a given
	 * socket index */
	ret = esmi_socket_power_avg_get(sock_id, &power);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] avgpower, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("socket[%d]/avg_power\t:%16.03f Watts \n",
			sock_id, (double)power/1000);
	/* Get the current power cap value for a given socket */
	ret = esmi_socket_power_cap_get(sock_id, &power);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] powercap, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("socket[%d]/power_cap\t:%16.03f Watts\n",
		sock_id, (double)power/1000);
	/* Get the max value that can be assigned as a power
	 * cap for a given socket */
	ret = esmi_socket_power_cap_max_get(sock_id, &power);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] maxpower, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("socket[%d]/power_cap_max\t:%16.03f Watts\n",
		sock_id, (double)power/1000);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_coreperf(uint32_t core_id)
{
	esmi_status_t ret;
	uint32_t boostlimit = 0;
	/* Get the boostlimit value for a given core */
	ret = esmi_core_boostlimit_get(core_id, &boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get core[%d] boostlimit, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("core[%d]/boostlimit\t: %u MHz\n", core_id, boostlimit);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_setpowercap(uint32_t sock_id, uint32_t power)
{
	esmi_status_t ret;
	uint32_t max_power = 0;

	ret = esmi_socket_power_cap_max_get(sock_id, &max_power);
	if ((ret == ESMI_SUCCESS) && (power > max_power)) {
		printf("Input power is more than max limit,"
			" So It set's to default max %.3f Watts\n",
			(double)max_power/1000);
		power = max_power;
	}
	ret = esmi_socket_power_cap_set(sock_id, power);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set socket[%d] powercap, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("Set socket[%d]/power_cap : %15.03f Watts successfully\n",
		sock_id, (double)power/1000);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_setcoreperf(uint32_t core_id, uint32_t boostlimit)
{
	esmi_status_t ret;
	uint32_t blimit = 0;

	ret = esmi_core_boostlimit_set(core_id,	boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set core[%d] boostlimit, Err[%d]: %s\n",
			core_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_core_boostlimit_get(core_id, &blimit);
	if (ret == ESMI_SUCCESS) {
		if (blimit < boostlimit) {
			printf("Set to max boost limit: %u MHz\n", blimit);
		} else if (blimit > boostlimit) {
			printf("Set to min boost limit: %u MHz\n", blimit);
		}
	}
	printf("Core[%d]/boostlimit set successfully\n", core_id);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_setsocketperf(uint32_t sock_id, uint32_t boostlimit)
{
	esmi_status_t ret;
	uint32_t blimit = 0;

	ret = esmi_socket_boostlimit_set(sock_id, boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set socket[%d] boostlimit, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_core_boostlimit_get(esmi_get_online_core_on_socket(sock_id),
				      &blimit);
	if (ret == ESMI_SUCCESS) {
		if (blimit < boostlimit) {
			printf("Set to max boost limit: %u MHz\n", blimit);
		} else if (blimit > boostlimit) {
			printf("Set to min boost limit: %u MHz\n", blimit);
		}
	}
	printf("Socket[%d]/boostlimit set successfully\n", sock_id);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_setpkgperf(uint32_t boostlimit)
{
	esmi_status_t ret;
	uint32_t blimit = 0;

	ret = esmi_package_boostlimit_set(boostlimit);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to set package boostlimit to %u MHz, "
			"Err[%d]: %s\n", boostlimit, ret,
			esmi_get_err_msg(ret));
		return ret;
	}
	ret = esmi_core_boostlimit_get(0, &blimit);
	if (ret == ESMI_SUCCESS) {
		if (blimit < boostlimit) {
			printf("Set to max boost limit: %u MHz\n", blimit);
		} else if (blimit > boostlimit) {
			printf("Set to min boost limit: %u MHz\n", blimit);
		}
	}
	printf("Package/boostlimit set successfully\n");
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_socktctl(uint32_t sock_id)
{
	esmi_status_t ret;
	uint32_t tctl = 0;

	ret = esmi_socket_tctl_get(sock_id, &tctl);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] tctl, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("socket[%d]/tctl	: %d °C\n", sock_id, tctl);
	return ESMI_SUCCESS;
}

esmi_status_t epyc_get_sockc0_residency(uint32_t sock_id)
{
	esmi_status_t ret;
	uint32_t residency = 0;

	ret = esmi_socket_c0_residency_get(sock_id, &residency);
	if (ret != ESMI_SUCCESS) {
		printf("Failed: to get socket[%d] residency, Err[%d]: %s\n",
			sock_id, ret, esmi_get_err_msg(ret));
		return ret;
	}
	printf("socket[%d]/c0_residency	: %u MHz\n", sock_id, residency);
	return ESMI_SUCCESS;
}

static void show_usage(char *exe_name)
{
	printf("Usage: %s [Option<s>] SOURCES\n"
	"Option<s>:\n"
	"\t-A, (--showall)\t\t\t\t\t\tGet all esmi parameter Values\n"
	"\t-l, (--showallenergy)\t\t\t\t\tGet energies for all cpus and "
        "sockets\n"
	"\t-e, (--showcoreenergy)\t  [CORENUM]\t\t\tGet energy for a given"
	" CPU\n"
	"\t-s, (--showsocketenergy)  [SOCKETNUM]\t\t\tGet energy for a given"
	" socket\n"
	"\t-p, (--showsocketpower)\t  [SOCKETNUM]\t\t\tGet power params for a"
	" given socket\n"
	"\t-L, (--showcoreboostlimit)[CORENUM]\t\t\tGet Boostlimit for a"
	" given CPU\n"
	"\t-C, (--setpowercap)\t  [SOCKETNUM] [POWERVALUE]\tSet power Cap"
	" for a given socket\n"
	"\t-a, (--setcoreboostlimit) [CORENUM] [BOOSTVALUE]\tSet boost limit"
	" for a given core\n"
	"\t-b, (--setsocketboostlimit)[SOCKETNUM] [BOOSTVALUE]\tSet Boost"
	" limit for a given Socket\n"
	"\t-c, (--setpkgboostlimit)  [PKG_BOOSTLIMIT]\t\tSet Boost limit"
	" for a given package\n"
	"\t-t, (--showsockettctl)\t  [SOCKETNUM]\t\t\tGet tctl for a given"
        " socket\n"
	"\t-r, (--showsocketc0residency) [SOCKETNUM]\t\tGet c0_residency for a"
	" given socket\n"
	"\t-h, (--help)\t\t\t\t\t\tShow this help message\n", exe_name);
}

void show_smi_message(void)
{
	printf("\n=============== EPYC System Management Interface ===============\n\n");
}

void show_smi_end_message(void)
{
	printf("\n====================== End of EPYC SMI Log =====================\n");
}

void show_smi_parameters(void)
{
	int id = 0;
	uint64_t core_input = 0, pkg_input = 0;
	uint32_t boostlimit = 0, avgpower = 0;
	uint32_t powercap = 0, powermax = 0;
	uint32_t cpus, sockets, threads, family, model;

	esmi_number_of_cpus_get(&cpus);
	esmi_number_of_sockets_get(&sockets);
	esmi_threads_per_core_get(&threads);
	esmi_cpu_family_get(&family);
	esmi_cpu_model_get(&model);

	printf("CPU family %xh, model %xh\n", family, model);
	printf("# NR_CPUS		| %8d | \n", cpus);
	printf("# NR_SOCKETS		| %8d | \n", sockets);
	printf("# THREADS PER CORE	| %8d | \n", threads);
	printf("# SMT			| %sabled | \n", (threads > 1) ? "en": "dis");

	/* Get the energy of the core for a given core index */
	esmi_core_energy_get(id, &core_input);
	/* Get the energy of the socket for a given socket index */
	esmi_socket_energy_get(id, &pkg_input);
	/* Get the Average power consumption for a given socket index */
	esmi_socket_power_avg_get(id, &avgpower);
	/* Get the current power cap value for a given socket */
	esmi_socket_power_cap_get(id, &powercap);
	/* Get the max value that can be assigned as a power cap
	 * for a given socket */
	esmi_socket_power_cap_max_get(id, &powermax);
	/* Get the boostlimit value for a given given core */
	esmi_core_boostlimit_get(id, &boostlimit);

	printf(RED "\n\nEnergy/Power/Boostlimit for CORE[0]/SOCKET[0]:" RESET"\n");
	printf("# SENSOR NAME		| 	Value in Units		|\n");
	printf("---------------------------------------------------------\n");
	printf( "# CORE_ENERGY		| %16ld uJoules	|\n"
		"# SOCKET_ENERGY		| %16ld uJoules	|\n"
		"# SOCKET_AVG_POWER	| %16.03f Watts	|\n"
		"# SOCKET_POWERCAP	| %16.03f Watts	|\n"
		"# SOCKET_MAX_POWERCAP	| %16.03f Watts	|\n"
		"# CORE_BOOSTLIMIT	| %16u MHz		|\n",
		core_input, pkg_input,
		(double)avgpower/1000, (double)powercap/1000,
		(double)powermax/1000, boostlimit);
}

void show_all_energy_counters(void)
{
	int i;
	uint64_t *input;
	uint32_t cpus;
	uint32_t sockets;
	uint32_t threads;
	esmi_status_t status;

	esmi_threads_per_core_get(&threads);
	esmi_number_of_cpus_get(&cpus);
	cpus = cpus/threads;
	esmi_number_of_sockets_get(&sockets);

	input = (uint64_t *) calloc(cpus + sockets, sizeof(uint64_t));
	if (NULL == input) {
		printf("Memory allocation failed all energy entries\n");
	}

	status = esmi_all_energies_get(input, cpus + sockets);
	if (status != ESMI_SUCCESS) {
		printf("Failed: to get all energies, Err[%d]: %s\n",
			status, esmi_get_err_msg(status));
		return;
	}
	for (i = 0; i < cpus + sockets; i++) {
		if (i < cpus) {
			printf("cpu  [%3d] : %12ld uJoules\n", i, input[i]);
		} else {
			printf("socket [%d] : %12ld uJoules\n", i - cpus, input[i]);

		}
	}
	free(input);
}

void show_smi_all_parameters(void)
{
	unsigned int i;
	uint64_t core_input, pkg_input;
	uint32_t avgpower, powercap, powermax;
	uint32_t boostlimit, tctl, residency;
	uint32_t cpu_count = 0;
	uint32_t socket_count = 0;

	/* get the number of cpus available */
	esmi_number_of_cpus_get(&cpu_count);
	/* get the number of sockets available */
	esmi_number_of_sockets_get(&socket_count);

	printf("_TOPOLOGY	| Count	     | \n");
	printf("#CPUS		| %10d | \n", cpu_count);
	printf("#SOCKETS	| %10d | \n\n", socket_count);

	printf("Core |       Energy Units   | Boost Limit  |\n");
	/* Get the energy of all the cores */
	for (i = 0; i < cpu_count; i++) {
		core_input = 0;
		boostlimit = 0;
		esmi_core_energy_get(i, &core_input);
		esmi_core_boostlimit_get(i, &boostlimit);
		printf("%3d  | %12ld uJoules | %u MHz     |\n",
			i, core_input, boostlimit);
	}
	printf("\nSocket |  _ENERGY               | _AVG_POWER   |"
			 " _POWERCAP     | _MAX_POWER_CAP |\n");
	for(i = 0; i < socket_count; i++) {
		/* Get the energy of all the sockets */
		pkg_input = 0;
		avgpower = 0;
		powercap = 0;
		powermax = 0;
		esmi_socket_energy_get(i, &pkg_input);
		/* Get the Average power consumption for all sockets */
		esmi_socket_power_avg_get(i, &avgpower);
		/* Get the current power cap value for all sockets */
		esmi_socket_power_cap_get(i, &powercap);
		/* Get the max value that can be assigned as a power cap
		 * for all sockets */
		esmi_socket_power_cap_max_get(i, &powermax);
		/* Get the tctl for all sockets */
		esmi_socket_tctl_get(i, &tctl);
		/* Get the  c0_residency for all sockets */
		esmi_socket_c0_residency_get(i, &residency);
		printf("%3d    | %12ld uJoules   | %6.03f Watts | %7.03f"
			" Watts | %7.03f Watts	|\n",
			i, pkg_input, (double)avgpower/1000,
			(double)powercap/1000, (double)powermax/1000);
	}
}

/*
 * returns 0 if the given string is a number, else 1
 */
static int is_string_number(char *str)
{
	int i;
	for (i = 0; str[i] != '\0'; i++) {
		if ((str[i] < '0') || (str[i] > '9')) {
			return 1;
		}
	}
	return ESMI_SUCCESS;
}

/**
Parse command line parameters and set data for program.
@param argc number of command line parameters
@param argv list of command line parameters
*/
esmi_status_t parsesmi_args(int argc,char **argv)
{
	esmi_status_t ret;
	int i;
	int opt = 0; /* option character */
	uint32_t core_id = 0, sock_id = 0;
	uint32_t power = 0, boostlimit = 0;
	static char *args[ARGS_MAX];
	char sudostr[] = "sudo";

	//Specifying the expected options
	static struct option long_options[] = {
		{"help",                no_argument,		0,	'h'},
		{"showall",             no_argument,		0,	'A'},
		{"showallenergy",       no_argument,		0,	'l'},
		{"showcoreenergy",      required_argument,	0,	'e'},
		{"showsocketenergy",	required_argument,	0,	's'},
		{"showsocketpower",     required_argument,	0,	'p'},
		{"showcoreboostlimit",	required_argument,	0,	'L'},
		{"setpowercap",         required_argument,	0,	'C'},
		{"setcoreboostlimit",	required_argument,	0,	'a'},
		{"setsocketboostlimit",	required_argument,	0,	'b'},
		{"setpkgboostlimit",	required_argument,	0,	'c'},
		{"showsockettctl",      required_argument,	0,	't'},
		{"showsocketc0residency", required_argument,	0,	'r'},
		{0,			0,			0,	0},
	};

	int long_index = 0;
	char *helperstring = "+hAle:s:p:L:C:a:b:c:t:r:";

	if (getuid() != 0) {
		while ((opt = getopt_long(argc, argv, helperstring,
				long_options, &long_index)) != -1) {
			switch (opt) {
				/* Below options requires sudo permissions to run */
				case 'C':
				case 'a':
				case 'b':
				case 'c':
					args[0] = sudostr;
					args[1] = argv[0];
					for (i = 0; i < argc; i++) {
						args[i + 1] = argv[i];
					}
					args[i + 1] = NULL;
					execvp("sudo", args);
			}
		}
	}
	show_smi_message();
	/* smi monitor objects initialization */
	ret = esmi_init();
	if(ret != ESMI_SUCCESS) {
		printf(RED "\tESMI Not initialized, drivers not found.\n"
		       "\tErr[%d]: %s\n" RESET, ret, esmi_get_err_msg(ret));
		return ret;
	}
	if (argc <= 1) {
		show_smi_parameters();
		printf(RED "\nTry `%s --help' for more information." RESET
			"\n\n", argv[0]);
	}
	optind = 0;
	while ((opt = getopt_long(argc, argv, helperstring,
			long_options, &long_index)) != -1) {
	if (opt == 'e' ||
	    opt == 's' ||
	    opt == 'p' ||
	    opt == 'L' ||
	    opt == 'C' ||
	    opt == 'a' ||
	    opt == 'b' ||
	    opt == 'c' ||
	    opt == 't' ||
	    opt == 'r') {
		if (is_string_number(optarg)) {
			printf("Option '-%c' require a valid numeric value"
					" as an argument\n\n", opt);
			show_usage(argv[0]);
			return ESMI_SUCCESS;
		}
	}
	if (opt == 'C' ||
	    opt == 'a' ||
	    opt == 'b') {
		// make sure optind is valid  ... or another option
		if (optind >= argc || *argv[optind] == '-') {
			printf("\nOption '-%c' require TWO arguments"
			 " <index>  <set_value>\n\n", optopt);
			show_usage(argv[0]);
			return ESMI_SUCCESS;
		}
		if(is_string_number(argv[optind])) {
			printf("Option '-%c' require 2nd argument as valid"
					" numeric value\n\n", opt);
			show_usage(argv[0]);
			return ESMI_SUCCESS;
		}
	}
	switch (opt) {
		case 'e' :
			/* Get the energy for a given core index */
			core_id = atoi(optarg);
			epyc_get_coreenergy(core_id);
			break;
		case 's' :
			/* Get the energy for a given socket index */
			sock_id = atoi(optarg);
			epyc_get_sockenergy(sock_id);
			break;
		case 'p' :
			/* Get the Power values for a given socket */
			sock_id = atoi(optarg);
			epyc_get_sockpower(sock_id);
			break;
		case 'L' :
			/* Get the Boostlimit for a given core index */
			core_id = atoi(optarg);
			epyc_get_coreperf(core_id);
			break;
		case 'C':
			/* Set the power cap value for a given socket */
			sock_id = atoi(optarg);
			power = atoi(argv[optind++]);
			epyc_setpowercap(sock_id, power);
			break;
		case 'a' :
			/* Set the boostlimit value for a given core */
			core_id = atoi(optarg);
			boostlimit = atoi(argv[optind++]);
			epyc_setcoreperf(core_id, boostlimit);
			break;
		case 'b' :
			/* Set the boostlimit value for a given socket */
			sock_id = atoi(optarg);
			boostlimit = atoi(argv[optind++]);
			epyc_setsocketperf(sock_id, boostlimit);
			break;
		case 'c' :
			/* Set the boostlimit value for a given package */
			boostlimit = atoi(optarg);
			epyc_setpkgperf(boostlimit);
			break;
		case 't' :
			/* Get the Thermal control for a given socket */
			sock_id = atoi(optarg);
			epyc_get_socktctl(sock_id);
			break;
		case 'r' :
			/* Get the Power values for a given socket */
			sock_id = atoi(optarg);
			epyc_get_sockc0_residency(sock_id);
			break;

		case 'A' :
			show_smi_all_parameters();
			break;
		case 'l' :
			show_all_energy_counters();
			break;
		case 'h' :
			show_usage(argv[0]);
			return ESMI_SUCCESS;
		case ':' :
			/* missing option argument */
			printf(RED "%s: option '-%c' requires an argument."
				RESET "\n\n", argv[0], optopt);
			break;
		case '?':
			if (isprint(optopt)) {
				printf(RED "Try `%s --help' for more"
				" information." RESET "\n", argv[0]);
			} else {
				printf("Unknown option character"
				" `\\x%x'.\n", optopt);
			}
			return ESMI_SUCCESS;
		default:
			printf(RED "Try `%s --help' for more information."
						RESET "\n\n", argv[0]);
			return ESMI_SUCCESS;
		} // end of Switch
	}
	for (i = optind; i < argc; i++) {
		printf(RED"\nExtra Non-option argument<s> passed : %s"
			RESET "\n", argv[i]);
		printf(RED "Try `%s --help' for more information."
					RESET "\n\n", argv[0]);
		return ESMI_SUCCESS;
	}

	return ESMI_SUCCESS;
}

int main(int argc, char **argv)
{
	/* Parse command arguments */
	parsesmi_args(argc, argv);

	show_smi_end_message();
	/* Program termination */
	esmi_exit();

	return ESMI_SUCCESS;
}

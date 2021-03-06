#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <rtdm/rtdm.h>
#include <native/task.h>
#include <native/sem.h>
#include <native/mutex.h>
#include <native/timer.h>
#include <rtdk.h>
#include <pthread.h>

#include "ecrt.h"

#define SM_FRAME_PERIOD_NS      4000000 // 4ms.
#define MINAS_ALIAS				0,0 // FIXME: This macro should be declared per slave.
#define MINAS	0x0000066f, 0x525100a1 // VendorId, ProductID

/* EtherCAT Configuration pointer */
static ec_master_t *master = NULL;

/* FIXME:  Declares per slave device. */
static ec_slave_config_t *slave0 = NULL;
/* FIXME: Process Data Offset in the domain. 
   Declares per slave device. */
unsigned int slave0_6041_00;
unsigned int slave0_6061_00;
unsigned int slave0_6064_00;
unsigned int slave0_6040_00;
unsigned int slave0_6060_00;
unsigned int slave0_60ff_00;
unsigned int slave0_607a_00;
unsigned int slave0_60b8_00;

/* FIXME: Declares per slave device. */
/*****************************************************************************/


/*****************************************************************************/


/* Slave 0, "MBDHT2510BD1"
 * Vendor ID:       0x0000066f
 * Product code:    0x525100a1
 * Revision number: 0x00010000
 */

ec_pdo_entry_info_t slave_0_pdo_entries[] = {
    {0x6040, 0x00, 16}, /* Controlword */
    {0x6060, 0x00, 8}, /* Modes of operation */
    {0x607a, 0x00, 32}, /* Target position */
    {0x60b8, 0x00, 16}, /* Touch probe function */
    {0x603f, 0x00, 16}, /* Error code */
    {0x6041, 0x00, 16}, /* Statusword */
    {0x6061, 0x00, 8}, /* Modes of operation display */
    {0x6064, 0x00, 32}, /* Position actual value */
    {0x60b9, 0x00, 16}, /* Touch probe status */
    {0x60ba, 0x00, 32}, /* Touch probe pos1 pos value */
    {0x60f4, 0x00, 32}, /* Following error actual value */
    {0x60fd, 0x00, 32}, /* Digital inputs */
};

ec_pdo_info_t slave_0_pdos[] = {
    {0x1600, 4, slave_0_pdo_entries + 0}, /* Receive PDO mapping 1 */
    {0x1a00, 8, slave_0_pdo_entries + 4}, /* Transmit PDO mapping 1 */
};

ec_sync_info_t slave_0_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, slave_0_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT, 1, slave_0_pdos + 1, EC_WD_DISABLE},
    {0xff}
};

/* Domains, yaskawa drive requires separated domain. */
static ec_domain_t *domain1 = NULL;
uint8_t *domain1_pd = NULL;

/* FIXME: Insert pdos after 1st slave code,
   if you want to add more slave devices.*/
const static ec_pdo_entry_reg_t domain1_regs[] = {
    {MINAS_ALIAS, MINAS, 0x6041, 0, &slave0_6041_00},
    {MINAS_ALIAS, MINAS, 0x6061, 0, &slave0_6061_00},
    {MINAS_ALIAS, MINAS, 0x6064, 0, &slave0_6064_00},
    {MINAS_ALIAS, MINAS, 0x6040, 0, &slave0_6040_00},
    {MINAS_ALIAS, MINAS, 0x6060, 0, &slave0_6060_00},
    {MINAS_ALIAS, MINAS, 0x607a, 0, &slave0_607a_00},
    {MINAS_ALIAS, MINAS, 0x60b8, 0, &slave0_60b8_00},
    {}
};

/* Handle exception */
static unsigned int sig_alarms = 0;
void signal_handler(int signum) {
	switch (signum) {
	case SIGALRM:
		sig_alarms++;
		break;
	}
}

RT_TASK my_task;
void cleanup_all(void) {
	printf("delete my_task\n");
	rt_task_delete(&my_task);
	ecrt_release_master(master);
}

int run; /* Execution flag for a cyclic task. */
void catch_signal(int sig) {
	run = 0;

	// Wait until task stops
	rt_task_join(&my_task);

	cleanup_all();
	printf("exit by signal\n");
	exit(0);
	return;
}

/* For monitoring Working Counter changes. */
ec_domain_state_t domain_state = {};
void check_domain_state (ec_domain_t *domain)
{
	ec_domain_state_t ds;

	ecrt_domain_state(domain, &ds);

	if (ds.working_counter != domain_state.working_counter)
		rt_printf("Domain: WC %u.\n", ds.working_counter);
	if (ds.wc_state != domain_state.wc_state)
		rt_printf("Domain: State %u.\n", ds.wc_state);

	domain_state = ds;
}

/* Xenomai task body */
void my_task_proc(void *arg)
{
	int ret;
	/* FIXME: Add variables if you add more slave devices. */
	unsigned short statusWord0, controlWord0;
	int actualPosition0, targetPosition0;
	
	run = 1;

	/* Set Xenomai task execution mode */
	ret = rt_task_set_mode(0, T_CONFORMING, NULL);
	if (ret) {
		rt_printf("error while rt_task_set_mode, code %d\n",ret);
		return;
	}
	/* Periodic execution */
	rt_task_set_periodic(NULL, TM_NOW, SM_FRAME_PERIOD_NS);
	
	/* Start pdo exchange loop until user stop */
	while (run) {
		// Retrieves datagram from ethercat frame(NIC driver->Master module).  
		ecrt_master_receive(master);
		// Processing datagram (Master module->domain).
		ecrt_domain_process(domain1);

		/* FIXME: Add macro when you use more slave devices.*/
		/* Get TxPDO data from domain. */
		statusWord0 = EC_READ_U16(domain1_pd + slave0_6041_00);
		actualPosition0 = EC_READ_S32(domain1_pd + slave0_6064_00);

		/* Not mandatory */
		check_domain_state (domain1);

		/* FIXME: Add following codes per each slave device..*/
		/* CiA 402 State machine according to the drive manual. */
		if (statusWord0 == 592 || statusWord0 == 624) controlWord0 = 6; //fprintf(stderr, "SwitchOnDisabled!!\n");
		else if (statusWord0 == 561) controlWord0 = 7; //fprintf(stderr, "Rdy2SwitchOn!!\n");
		else if (statusWord0 == 563) controlWord0 = 15; //fprintf(stderr, "SwitchedOn!!\n");
		//else if (statusWord == 55 || statusWord == 1079) ;
		else if (statusWord0 == 567) controlWord0 = 15; //fprintf(stderr, "Operation Enabled!!\n");
		else controlWord0 = 128; // Other initial state.


		#if 0
		if ((statusWord0 & 0x4f) == 0x40)	controlWord0 = 0x06; //fprintf(stderr, "SwitchOnDisabled!!\n");
		else if ((statusWord0 & 0x6f) == 0x21) controlWord0 = 0x07; //fprintf(stderr, "Rdy2SwitchOn!!\n");
		else if ((statusWord0 & 0x027f) == 0x233) controlWord0 = 0x0f; //fprintf(stderr, "SwitchedOn!!\n");
		else if ((statusWord0 & 0x027f) == 0x237) controlWord0 = 0x1f; //fprintf(stderr, "Operation Enabled!!\n");
		else controlWord0 = 0x80; //fprintf(stderr, "Unknown!!!!\n");
		#endif
		/* FIXME: Add macro when you use more slave devices.*/
		/* Write RxPDO to the domain */

		targetPosition0 = actualPosition0 + 10000; /* Set target position */

	EC_WRITE_U16(domain1_pd + slave0_6040_00, controlWord0);
    	EC_WRITE_S32(domain1_pd + slave0_607a_00, targetPosition0);
//    	EC_WRITE_S32(domain1_pd + slave0_60ff_00, 0x000FFFFF);
//    	EC_WRITE_S8(domain1_pd + slave0_6060_00, 0x08); // CSP


		/* Queueing RxPDO to datagram (Domain->Master module). */
		ecrt_domain_queue(domain1);
		/* Transmission EtherCAT frame */
		ecrt_master_send(master);
		/* Wait until next release point */
		rt_task_wait_period(NULL);
	}
}

static unsigned int cycle_ns = SM_FRAME_PERIOD_NS;

int main(int argc,char **argv)
{
	int ret;
	uint32_t abort_code;

	/* Lock all currently mapped pages to prevent page swapping */
	mlockall(MCL_CURRENT | MCL_FUTURE);

	/* Register signal handler */
	signal(SIGTERM, catch_signal);
	signal(SIGINT, catch_signal);

	/* Request EtherCAT master */
	master = ecrt_request_master(0);
	if (!master) return -1;

	/* Create domain for PDO exchange. */
	domain1 = ecrt_master_create_domain(master);
	if (!domain1) return -1;
	
	/* PDO configuration for slave device */ 
	/* FIXME: Use following codes per slave device */
	if (!(slave0 = ecrt_master_slave_config(master, MINAS_ALIAS, MINAS))) {
		fprintf(stderr, "Failed to get slave 0 and position 0.\n");
		return -1;
	}

	if (ecrt_slave_config_pdos(slave0, EC_END, slave_0_syncs)) {
		fprintf(stderr, "Failed to configure PDOs for slave 0 and position 0.\n");
		return -1;
	}

	// FIXME: Add here when you use more slave devices. 

	/* Setup EtherCAT Master transmit interval(us), 
	   usually same as the period of control task. */
	ecrt_master_set_send_interval(master, cycle_ns / 1000);

	/* Register pdo entry to the domains */
	if (ecrt_domain_reg_pdo_entry_list(domain1, domain1_regs)) {
		fprintf(stderr, "PDO entry registration failed!\n");
		return -1;
	}

	/* Setup Drive operation mode via SDO.*/
	/* Default value of received drive is 9 now. */
	{
		/* FIXME: Use following codes when multiple drives are required. */
		uint8_t value[1];
		EC_WRITE_S8((uint8_t *)value, 0x08);
		if(ecrt_master_sdo_download(master, 0, 0x6060, 0x00, (uint8_t *)value, 1, &abort_code))
		{
			fprintf(stderr, "Failed to initialize slave 0 and position 0,\nError: %d\n", abort_code);
			return -1;
		}
	}

	/* Activating master stack */
	if (ecrt_master_activate(master))
		return -1;

	/* Debugging purpose, get the address of mapped domains. */
	if (!(domain1_pd = ecrt_domain_data(domain1))) return -1;
	fprintf(stdout, "Master 0 activated...\n\n");
	fprintf(stdout, "domain1_pd:  0x%.6lx\n", (unsigned long)domain1_pd);

	/* Creating cyclic xenomai task */
	ret = rt_task_create(&my_task,"my_task",0,50,T_JOINABLE);

	/* Starting cyclic task */
	fprintf(stdout, "starting my_task\n");
	ret = rt_task_start(&my_task, &my_task_proc, NULL);

	while (run)	{
		sched_yield();
	}

	/* Cleanup routines. */ 
	rt_task_join(&my_task);
	rt_task_delete(&my_task);
	fprintf(stdout, "End of Program\n");
	ecrt_release_master(master); /* Releases a requested EtherCAT master */

	return 0;
}

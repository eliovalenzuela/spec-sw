/*
 * Tool to control/read xwr_transmission, based on specmem
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <time.h>

#include "speclib.h"
#include "wr_transmission.h"
#include "WRBtrain.h"

#define OPT_HELP        111
#define OPT_BUS_PCI      1
#define OPT_BUS_PCI_B    2
#define OPT_BUS_PCI_D    3
#define OPT_BUS_VME      4
#define OPT_STATS        5
#define OPT_RESET        7
#define OPT_RESET_NA     8
#define OPT_RESET_CNTS   9
#define OPT_RESET_SEQID  10
#define OPT_RESET_TIME   11
#define OPT_TX_ETHERTYPE 12
#define OPT_TX_LOC_MAC   13
#define OPT_TX_TAR_MAC   14
#define OPT_RX_ETHERTYPE 15
#define OPT_RX_LOC_MAC   16
#define OPT_RX_REM_MAC   17
#define OPT_TX_CONF_ENA  18
#define OPT_RX_CONF_ENA  19
#define OPT_DBG_BYTE     20
#define OPT_DBG_MUX      21
#define OPT_DBG_VAL      22
#define OPT_TX_BVAL      23
#define OPT_RX_BVAL      24
#define OPT_DUMMY        25
#define OPT_RXTX_CONF    26
#define OPT_GET_LATENCY  27
#define OPT_SET_LATENCY  28
#define OPT_SET_TXDBG    29
#define OPT_SEND_FRAME   30
#define OPT_SET_TXPER    31
#define OPT_SET_VALINV   32
#define OPT_SET_VALPER   33
#define OPT_SET_VALDEL   34

#define OPT_UNKNOWN    100

#define BUS_PCI 0
#define BUS_VME 1

#define LEAP_SECONDS 27


/* runtime options */
static struct option ropts[] = {
  {"help",            0, NULL, OPT_HELP},                        /* 0 */
  {"vme",             0, NULL, OPT_BUS_VME},                     /* 1 */
  {"pci",             0, NULL, OPT_BUS_PCI},                     /* 2 */
  {"b",               1, NULL, OPT_BUS_PCI_B},                   /* 3 */
  {"d",               1, NULL, OPT_BUS_PCI_D},                   /* 4*/
  {"stats",           0, NULL, OPT_STATS},                       /* 5*/
  {"reset",           0, NULL, OPT_RESET},                       /* 6*/
  {"res_cnts",        0, NULL, OPT_RESET_CNTS},                  /* 7*/
  {"res_seqid",       0, NULL, OPT_RESET_SEQID},                 /* 8*/
  {"txEthertype",     1, NULL, OPT_TX_ETHERTYPE},                /* 9*/
  {"txLocalMAC",      1, NULL, OPT_TX_LOC_MAC},                  /* 10*/
  {"txTargetMAC",     1, NULL, OPT_TX_TAR_MAC},                  /* 11*/
  {"rxEthertype",     1, NULL, OPT_RX_ETHERTYPE},                /* 12*/
  {"rxLocalMAC",      1, NULL, OPT_RX_LOC_MAC},                  /* 13*/
  {"rxRemoteMAC",     1, NULL, OPT_RX_REM_MAC},                  /* 14*/
  {"getLatency",      0, NULL, OPT_GET_LATENCY},                 /* 15*/
  {"setLatency",      1, NULL, OPT_SET_LATENCY},                 /* 16*/
  {"debugByte",       1, NULL, OPT_DBG_BYTE},                    /* 17*/
  {"debugMux",        1, NULL, OPT_DBG_MUX},                     /* 18*/
  {"debugValue",      1, NULL, OPT_DBG_VAL},                     /* 19*/
  {"txBvalue",        0, NULL, OPT_TX_BVAL},                     /* 20*/
  {"rxBvalue",        0, NULL, OPT_RX_BVAL},                     /* 21*/
  {"btTxDbg",         1, NULL, OPT_SET_TXDBG},                   /* 22*/
  {"btTxSingle",      0, NULL, OPT_SEND_FRAME},                  /* 23*/
  {"btTxPeriod",      1, NULL, OPT_SET_TXPER},                   /* 24*/
  {"btRxValidInv",    1, NULL, OPT_SET_VALINV},                  /* 25*/
  {"btRxValidPeriod", 1, NULL, OPT_SET_VALPER},                  /* 26*/
  {"btRxValidDelay",  1, NULL, OPT_SET_VALDEL},                  /* 27*/
  {0,}};

static char description[][100] = {
  {" - show this help"},                                           /* 0*/
  {" - use VME bus to access WB registers"},                       /* 1*/
  {" - use PCI bus to access WB registers (default)"},             /* 2*/
  {" - set PCI bus number (default 0x1)"},                         /* 3*/
  {" - set PCI device number (default 0x0)"},                      /* 4*/
  {" - show all streamers statistics"},                            /* 5*/
  {" - show time of the latest reset / time elapsed since then"},  /* 6*/
  {" - reset tx/rx/lost counters and avg/min/max latency values"}, /* 7*/
  {" - reset sequence ID of the tx streamer"},                     /* 8*/
  {" - set TX ethertype"},                                         /* 9*/
  {" - set TX Local  MAC addres"},                                 /* 10*/
  {" - set TX Target MAC address"},                                /* 11*/
  {" - set RX ethertype"},                                         /* 12*/
  {" - set RX Local  MAC adddress"},                               /* 13*/
  {" - set RX Remote Mac address"},                                /* 14*/
  {" - get config of fixed latency in integer [us]"},              /* 15*/
  {" - set config of fixed latency in integer [us] (-1 to disable)"},/* 16*/
  {" - set which byte of the rx or tx frame should be snooped"},   /* 17*/
  {" - set whether tx or rx frames should be snooped"},            /* 18*/
  {" - read the snooped 32-bit value"},                            /* 19*/
  {" - read tx Bvalue"},                                           /* 20*/
  {" - read rx Bvalue"},                                           /* 21*/
  {" - set tx debugging of btrain (0:disable, 1: enable"},         /* 22*/
  {" - send a single btrian frame"},                               /* 23*/
  {" - set transmission period in integer [us]"},                  /* 24*/
  {" - invert the valid signal (0: HIGH valid, 1: LOW valid)"},    /* 25*/
  {" - set valid period in [us] (0x0000:output disabled)"},        /* 26*/
  {" - set valid delay in [us] "}};                                 /* 27*/

static int   wr_trans_base=0x1100;
static int   btrain_base  =0x1200;
static void *card=0;
static int bus = 1, dev_fn = 0;
static int   bus_type = BUS_PCI;
static char *prgname;

typedef enum {
  UNKNOWN,
  RESET,
  STATS,
  RESET_TIME} cmd_t;

void help(char *name)
{
  int i = 0;

  fprintf(stderr, "The following options are possible with/without argument:\n");
  fprintf(stderr, "Use: \"%s <option> <arg>\"\n",name);
  while(ropts[i].name){
    if(ropts[i].has_arg)
      fprintf(stderr, "\t --%-15s [argument] %s\n",ropts[i].name, description[i]);
    else
      fprintf(stderr, "\t --%-15s            %s\n", ropts[i].name, description[i]);
    i++;
  }
  exit(1);
}
void open_access(void)
{
  card = spec_open(bus, dev_fn);
  if (!card) {
    fprintf(stderr, "%s: No SPEC card at bus %i, devfn %i\n",
      prgname, bus, dev_fn);
    fprintf(stderr, "  please make sure the address is correct,\n"
      "  spec.ko is loaded and you run as superuser.\n");
    exit(1);
  }
}

void write_transmission(uint32_t data, uint32_t addr)
{
  if(card ==0) open_access();
  if(bus_type == BUS_PCI) spec_writel(card, data, wr_trans_base+addr);
}

uint32_t read_transmission(uint32_t addr)
{
  if(card ==0) open_access();
  if(bus_type == BUS_PCI) return spec_readl(card, wr_trans_base+addr);
}

void write_btrain(uint32_t data, uint32_t addr)
{
  if(card ==0) open_access();
  if(bus_type == BUS_PCI) spec_writel(card, data, btrain_base+addr);
}

uint32_t read_btrain(uint32_t addr)
{
  if(card ==0) open_access();
  if(bus_type == BUS_PCI) return spec_readl(card, btrain_base+addr);
}


void read_reset_time(void)
{
  uint32_t val=0;
  int days=0, hours=0, minutes=0, seconds;
  double reset_time_elapsed=0;
  time_t cur_time;
  time_t res_time_sec;

  val=read_transmission(WR_TRANSMISSION_REG_SSCR2);
  res_time_sec = (time_t)(WR_TRANSMISSION_SSCR2_RST_TS_TAI_LSB_R(val) + LEAP_SECONDS);//to UTC

  cur_time           = time(NULL);
  reset_time_elapsed = difftime(cur_time,res_time_sec);
  days               =  reset_time_elapsed/(60*60*24);
  hours              = (reset_time_elapsed-days*60*60*24)/(60*60);
  minutes            = (reset_time_elapsed-days*60*60*24-hours*60*60)/(60);
  seconds            = (reset_time_elapsed-days*60*60*24-hours*60*60-minutes*60);
  fprintf(stderr, "Time elapsed from reset: %d days, %d h, %d m, %d s; Reseted on %s\n", 
    days, hours, minutes, seconds, asctime(localtime(&res_time_sec)));
}

void read_all_stats(void)
{
  uint32_t max_latency_raw= 0, min_latency_raw=0;
  double   max_latency= 0, min_latency=0;
  uint32_t rx_cnt= 0, tx_cnt=0, rx_cnt_lost_fr=0, rx_cnt_lost_blk=0;
  uint64_t latency_acc =0;
  uint32_t latency_acc_lsb=0, latency_acc_msb=0, latency_cnt=0, val=0;
  int overflow=0;
  double latency_avg=0;

  //snapshot stats
  write_transmission(WR_TRANSMISSION_SSCR1_SNAPSHOT_STATS, WR_TRANSMISSION_REG_SSCR1);

  // min/max
  max_latency_raw  = read_transmission(WR_TRANSMISSION_REG_RX_STAT3);
  max_latency      = (WR_TRANSMISSION_RX_STAT3_RX_LATENCY_MAX_R(max_latency_raw)*8)/1000.0;
  min_latency_raw  = read_transmission(WR_TRANSMISSION_REG_RX_STAT4);
  min_latency = (WR_TRANSMISSION_RX_STAT4_RX_LATENCY_MIN_R(min_latency_raw)*8)/1000.0;

  //cnts
  tx_cnt           = read_transmission(WR_TRANSMISSION_REG_TX_STAT);
  rx_cnt           = read_transmission(WR_TRANSMISSION_REG_RX_STAT1);
  rx_cnt_lost_fr   = read_transmission(WR_TRANSMISSION_REG_RX_STAT2);
  rx_cnt_lost_blk  = read_transmission(WR_TRANSMISSION_REG_RX_STAT8);

  //read values
  latency_acc_lsb  = read_transmission(WR_TRANSMISSION_REG_RX_STAT5);
  latency_acc_msb  = read_transmission(WR_TRANSMISSION_REG_RX_STAT6);
  latency_cnt      = read_transmission(WR_TRANSMISSION_REG_RX_STAT7);
  val              = read_transmission(WR_TRANSMISSION_REG_RX_STAT7);
  overflow         = (WR_TRANSMISSION_SSCR1_RX_LATENCY_ACC_OVERFLOW & val) != 0;
  //put it all together
  latency_acc     = (((uint64_t)latency_acc_msb) << 32) | latency_acc_lsb;
  latency_avg     = (((double)latency_acc)*8/1000)/(double)latency_cnt;

  //release snapshot
  write_transmission(0, WR_TRANSMISSION_REG_SSCR1);

  fprintf(stderr, "Latency [us]    : min=%10g max=%10g avg =%10g (0x%x, 0x%x, %lld=%u << 32 | %u)*8/1000 us, cnt=%u)\n",
    min_latency, max_latency, latency_avg,
    min_latency_raw, max_latency_raw, (long long)latency_acc, latency_acc_msb, latency_acc_lsb, latency_cnt);
  fprintf(stderr, "Frames  [number]: tx =%10u rx =%10u lost=%10u (lost blocks%5u)\n",
    tx_cnt, rx_cnt, rx_cnt_lost_fr,rx_cnt_lost_blk);
}

void read_max_min_latency(void)
{
  uint32_t max_latency_raw= 0, min_latency_raw=0;
  double max_latency= 0, min_latency=0;

  max_latency_raw  = read_transmission(WR_TRANSMISSION_REG_RX_STAT3);
  max_latency      = (WR_TRANSMISSION_RX_STAT3_RX_LATENCY_MAX_R(max_latency_raw)*8)/1000.0;
  min_latency_raw  = read_transmission(WR_TRANSMISSION_REG_RX_STAT4);
  min_latency = (WR_TRANSMISSION_RX_STAT4_RX_LATENCY_MIN_R(min_latency_raw)*8)/1000.0;
  fprintf(stderr, "Latency: mix=%g max=%g [us] (0x%x | 0x%x)\n",
    min_latency, max_latency, min_latency_raw, max_latency_raw);
}

void read_cnts(void)
{
  uint32_t rx_cnt= 0, tx_cnt=0, rx_cnt_lost_fr=0, rx_cnt_lost_blk=0;

  tx_cnt          = read_transmission(WR_TRANSMISSION_REG_TX_STAT);
  rx_cnt          = read_transmission(WR_TRANSMISSION_REG_RX_STAT1);
  rx_cnt_lost_fr  = read_transmission(WR_TRANSMISSION_REG_RX_STAT2);
  rx_cnt_lost_blk = read_transmission(WR_TRANSMISSION_REG_RX_STAT8);
  fprintf(stderr, "Frames: \n\ttx=%5u \n\trx=%5u \n\tlost=%5u (lost blocks%5u)\n",
    tx_cnt, rx_cnt, rx_cnt_lost_fr,rx_cnt_lost_blk);
}

// void read_avg_latency(void)
// {
//   uint64_t latency_acc =0;
//   uint32_t latency_acc_lsb=0, latency_acc_msb=0, latency_cnt=0, val=0;
//   int overflow=0;
//   double latency_avg=0;
// 
//   //snapshot stats
//   write_transmission(WR_TRANSMISSION_SSCR1_SNAPSHOT_STATS, WR_TRANSMISSION_REG_SSCR1);
// 
//   //read values
//   latency_acc_lsb = read_transmission(WR_TRANSMISSION_REG_RX_STAT5);
//   latency_acc_msb = read_transmission(WR_TRANSMISSION_REG_RX_STAT6);
//   latency_cnt     = read_transmission(WR_TRANSMISSION_REG_RX_STAT7);
//   val             = read_transmission(WR_TRANSMISSION_REG_RX_STAT7);
//   overflow        = (WR_TRANSMISSION_SSCR1_RX_LATENCY_ACC_OVERFLOW & val) != 0;
//   //put it all together
//   latency_acc     = (((uint64_t)latency_acc_msb) << 32) | latency_acc_lsb;
//   latency_avg     = (((double)latency_acc)*8/1000)/(double)latency_cnt;
// 
//   //release snapshot
//   write_transmission(0, WR_TRANSMISSION_REG_SSCR1);
//   if(overflow)
//     fprintf(stderr, "WARNING: the latency accumulator has overflown, so the avg value is crap\n");
//   fprintf(stderr, "Latency avg: %g acc=(%lld=%u << 32 | %u)*8/1000 us, cnt=%u]\n",
//     latency_avg, (long long)latency_acc, latency_acc_msb, latency_acc_lsb, latency_cnt);
// }

void reset_counters()
{
  write_transmission(WR_TRANSMISSION_SSCR1_RST_STATS, WR_TRANSMISSION_REG_SSCR1);
  fprintf(stderr, "Reseted statistics counters\n");
}


void reset_seqid()
{
  write_transmission(WR_TRANSMISSION_SSCR1_RST_SEQ_ID, WR_TRANSMISSION_REG_SSCR1);
  fprintf(stderr, "Reseted message sequence id\n");
}

void set_latency(int latency)
{
  uint32_t set_val=0,read_val=0;
  if(latency < 0){
    read_val = read_transmission(WR_TRANSMISSION_REG_CFG);
    set_val  = ~WR_TRANSMISSION_CFG_OR_RX_FIX_LAT & read_val;
    write_transmission(set_val, WR_TRANSMISSION_REG_CFG);
    fprintf(stderr, "Disabled overriding of default fixed latency value"
      "(it is now the default/set by application)\n");
  }
  else{
    set_val  = (latency*1000)/8;
    write_transmission(WR_TRANSMISSION_RX_CFG5_FIXED_LATENCY_W(set_val), WR_TRANSMISSION_REG_RX_CFG5);
    read_val = read_transmission(WR_TRANSMISSION_REG_CFG);
    set_val  = WR_TRANSMISSION_CFG_OR_RX_FIX_LAT | read_val;
    write_transmission(set_val, WR_TRANSMISSION_REG_CFG);
    read_val = WR_TRANSMISSION_RX_CFG5_FIXED_LATENCY_R(read_transmission(WR_TRANSMISSION_REG_RX_CFG5));
    fprintf(stderr, "Fixed latency set: %d [us] (set %d | read : %d [cycles])\n",
      latency,((latency*1000)/8), read_val);
  }
}

void read_latency(void)
{
  uint32_t read_val=0;
  read_val = WR_TRANSMISSION_RX_CFG5_FIXED_LATENCY_R(read_transmission(WR_TRANSMISSION_REG_RX_CFG5));
  fprintf(stderr, "Fixed latency configured: %d [us]\n", read_val);
}
void read_tx_bval(void)
{
  uint32_t read_val=0;
  read_val = read_transmission(WR_TRANSMISSION_REG_DBG_TX_BVALUE);
  fprintf(stderr, "%d\n", read_val);
}

void read_rx_bval(void)
{
  uint32_t read_val=0;
  read_val = read_transmission(WR_TRANSMISSION_REG_DBG_RX_BVALUE);
  fprintf(stderr, "%d\n", read_val);
}

void set_bt_debug(int argument)
{
  uint32_t val=0;
  if(argument==1)      {fprintf(stderr, "Enable  Brain debug\n"); val=1;}
  else if(argument==0) {fprintf(stderr, "Disable Brain debug\n"); val=0;}
  else return;
  write_btrain(WRBTRAIN_SCR_TX_DBG_W(val),WRBTRAIN_REG_SCR);
}

void send_bt_single_frame(void)
{
  write_btrain(WRBTRAIN_SCR_TX_SINGLE,WRBTRAIN_REG_SCR);
  fprintf(stderr, "Send single btrain frame\n");
}

void set_bt_tx_period(int argument)
{
  uint32_t val=0;
  val = (argument*1000)/16;
  write_btrain(WRBTRAIN_TX_PERIOD_VALUE_W(val),WRBTRAIN_REG_TX_PERIOD);
  fprintf(stderr, "Send with period %d [us] = %d cycles (0x%x)\n",
    argument, val, val);
}

void set_bt_valid_period(int argument)
{
  uint32_t set_val=0, get_val=0;
  set_val = argument;
  write_btrain(WRBTRAIN_RX_OUT_DATA_TIME_VALID_W(set_val),WRBTRAIN_REG_RX_OUT_DATA_TIME);
  get_val = WRBTRAIN_RX_OUT_DATA_TIME_VALID_R(read_btrain(WRBTRAIN_REG_RX_OUT_DATA_TIME));
  fprintf(stderr, "Send rx valid period period %d [ns] = %d [cycles] (0x%x) | get=%d\n",
    set_val*16, set_val, get_val);
}
int main(int argc, char **argv)
{
  int c;
  int argument=0;
  prgname = argv[0];

  if (argc == 1) {
    help(prgname);
    exit(0);
  }

  /*parse parameters*/
  while( (c = getopt_long(argc, argv, "h", ropts, NULL)) != -1) {
    switch(c) {
      case OPT_HELP:
        help(prgname);
        break;
      case OPT_BUS_PCI:
        bus_type = BUS_PCI;
        break;
      case OPT_BUS_PCI_B:
        bus=atoi(optarg);
        break;
      case OPT_BUS_PCI_D:
        dev_fn=atoi(optarg);
        break;
      case OPT_STATS:
        read_all_stats();
        //read_max_min_latency();
        //read_cnts();
        //read_avg_latency();
        break;
      case OPT_RESET_SEQID:
        reset_seqid();
        break;
      case OPT_RESET_CNTS:
        reset_counters();
        break;
      case OPT_RESET:
        read_reset_time();
        fprintf(stderr, "For reset action, use:"
          "\n\t%s --res_seqid - to reset sequence id"
          "\n\t%s --res_cnts  - to reset counters\n", prgname,prgname);
        break;
      case OPT_SET_LATENCY:
        argument = atoi(optarg);
        set_latency(argument);
        break;
      case OPT_GET_LATENCY:
        argument = atoi(optarg);
        read_latency();
        break;
      case OPT_TX_BVAL: 
        read_tx_bval();
        break;
      case OPT_RX_BVAL: 
        read_rx_bval();
        break;
      case OPT_SET_TXDBG:
        argument = atoi(optarg);
        set_bt_debug(argument);
        break;
      case OPT_SEND_FRAME:
        send_bt_single_frame();
        break;
      case OPT_SET_TXPER:
        argument = atoi(optarg);
        set_bt_tx_period(argument);
        break;
      case OPT_SET_VALPER:
        argument = atoi(optarg);
        set_bt_valid_period(argument);
        break;
      case OPT_BUS_VME:
      case OPT_RXTX_CONF:
      case OPT_TX_ETHERTYPE:
      case OPT_TX_LOC_MAC:
      case OPT_TX_TAR_MAC:
      case OPT_RX_ETHERTYPE:
      case OPT_RX_LOC_MAC:
      case OPT_RX_REM_MAC:
      case OPT_DBG_BYTE:
      case OPT_DBG_MUX:
      case OPT_DBG_VAL:
      case OPT_SET_VALINV:
      case OPT_SET_VALDEL:
        fprintf(stderr, "unimplemented option\n");
        break;
      default:
        help(prgname);
    }
  }

  spec_close(card);

  exit (0);
}

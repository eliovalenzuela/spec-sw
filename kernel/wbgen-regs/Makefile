# This Makefile is used to reproduce the headers from svn checkout.
# You need to have "wbgen2" in your command search path and the white-rabbit
# repos (wr-cores and wr-switch-hdl) Git checkout in $REPOS.  
# Since this is only meant to be used by me (or Tom) no serious checking is done.

# List of input files in Git checkout

MODULES_WRS ?= $(REPOS)/wr-switch-hdl/modules
MODULES_WRC ?= $(REPOS)/wr-cores/modules

#SPECS = $(HW_REPO)/trunk/documentation/specifications

WB_ENDPOINT = $(MODULES_WRC)/wr_endpoint/ep_wishbone_controller.wb
WB_MDIO 		= $(MODULES_WRC)/wr_endpoint/pcs_regs.wb
WB_PPSG     = $(MODULES_WRC)/wr_pps_gen/pps_gen_wb.wb
WB_TSTAMP   = $(MODULES_WRS)/wrsw_txtsu/wrsw_txtsu.wb
WB_RTU      = $(MODULES_WRS)/wrsw_rtu/rtu_wishbone_slave.wb
WB_NIC      = $(MODULES_WRS)/wrsw_nic/wr_nic.wb
WB_SOFTPLL  = $(MODULES_WRC)/wr_softpll_ng/spll_wb_slave.wb

HEADERS = endpoint-regs.h endpoint-mdio.h ppsg-regs.h tstamp-regs.h rtu-regs.h \
          nic-regs.h softpll-regs.h
WBINPUT = $(HEADERS:.h=wb)

# No default, for people who types "make" everywhere (like me)
all:
	@echo "This is for developer's use, see Makefile for details"
	exit 1

# The headers rule regenerates headers from local wb files
headers: $(HEADERS)

%.h: %.wb
	wbgen2 --cstyle=struct  --co=$@ $<
	sed -i 's,#include <inttypes.h>,#ifdef __KERNEL__\n#include <linux/types.h>\n#else\n#include <stdint.h>\n#endif\n,' $@
	sed -i '/ Created *: .*20[0-9][0-9]$$/ d' $@
	sed -i 's/-REGS_WB//' $@


# The wbinput rule just copies here stuff from svn.
# Do it silent so errors stand out
wbinput:
	@cp $(WB_ENDPOINT) endpoint-regs.wb
	@cp $(WB_MDIO) endpoint-mdio.wb
	@cp $(WB_PPSG) ppsg-regs.wb
	@cp $(WB_TSTAMP) tstamp-regs.wb
	@cp $(WB_RTU) rtu-regs.wb
	@cp $(WB_NIC) nic-regs.wb
	@cp $(WB_SOFTPLL) softpll-regs.wb
	@echo "Copied input files from subversions to local directory"

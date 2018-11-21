// SPDX-License-Identifier: GPLv2
/*
 * Copyright (C) 2017 CERN (www.cern.ch)
 * Author: Federico Vaga <federico.vaga@cern.ch>
 */

#ifndef __SPEC_COMPAT_H__
#define __SPEC_COMPAT_H__

#include <linux/version.h>


/*
 * The way this function is used in this code is to enable MSI. So,
 * this compat function assumes that we always do that and so we
 * ignore flags
 */
#if KERNEL_VERSION(4,11,0) > LINUX_VERSION_CODE
int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
			  unsigned int max_vecs, unsigned int flags) {
#if KERNEL_VERSION(3, 16, 0) > LINUX_VERSION_CODE
        return pci_enable_msi_block(dev, min_vecs);
#else
        return pci_enable_msi_exact(dev, min_vecs);
#endif
}

void pci_free_irq_vectors(struct pci_dev *pdev) {
	pci_disable_msi(pdev);
}

#endif


#endif

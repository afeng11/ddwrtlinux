/*
 * Copyright 2000 Deep Blue Solutions Ltd
 * Copyright 2004 ARM Limited
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __CNS3XXX_CORE_H
#define __CNS3XXX_CORE_H

#include <linux/reboot.h>

extern void cns3xxx_timer_init(void);

void __init cns3xxx_common_init(void);
void __init cns3xxx_init_irq(void);
void cns3xxx_power_off(void);
void cns3xxx_restart(enum reboot_mode, const char *);
extern void __init cns3xxx_pcie_init_late(void);
extern void __init cns3xxx_pcie_set_irqs(int bus, int *irqs);

#endif /* __CNS3XXX_CORE_H */

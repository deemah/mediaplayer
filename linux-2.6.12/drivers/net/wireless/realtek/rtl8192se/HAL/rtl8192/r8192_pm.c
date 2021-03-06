/* 
   Power management interface routines. 
   Written by Mariusz Matuszek.
   This code is currently just a placeholder for later work and 
   does not do anything useful.
   
   This is part of rtl8180 OpenSource driver.
   Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it> 
   Released under the terms of GPL (General Public Licence)	
*/

#ifdef CONFIG_PM_RTL

#ifdef RTL8192SE
#include "r8192E.h"
#include "r8192SE_hw.h"
#include "r8192S_phy.h"
#include "r8192S_phyreg.h"
#include "r8192_pm.h"
#include "r8192S_rtl6052.h"
#else
#include "r8192E.h"
#include "r8192E_hw.h"
#include "r8192_pm.h"
#include "r8190_rtl8256.h"
#endif

int rtl8192E_save_state (struct pci_dev *dev, pm_message_t state)
{
        //printk(KERN_NOTICE "r8192E save state call (state %u).\n", state.event);
	return(-EAGAIN);
}


int rtl8192E_suspend (struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct r8192_priv *priv = ieee80211_priv(dev); 
	//u8	ucRegRead;	
	//u32	ulRegRead;

        RT_TRACE(COMP_POWER, "============> r8192E suspend call.\n");
        printk("============> r8192E suspend call.\n");
#ifdef ENABLE_GPIO_RADIO_CTL
	del_timer_sync(&priv->gpio_polling_timer);
	cancel_delayed_work(&priv->gpio_change_rf_wq);
	priv->polling_timer_on = 0;
#endif

        if (!netif_running(dev)){
            printk("RTL819XE:UI is open out of suspend function\n");
            goto out_pci_suspend;
        }

	dev->stop(dev);
        //lzm 090331 just no UI call it
	netif_device_detach(dev);

	// Call MgntActSet_RF_State instead to prevent RF config race condition.
	// By Bruce, 2008-01-17.
	//
#ifndef RTL8192SE		
	if(!priv->ieee80211->bSupportRemoteWakeUp) {
		MgntActSet_RF_State(dev, eRfOff, RF_CHANGE_BY_INIT);
		// 2006.11.30. System reset bit
		ulRegRead = read_nic_dword(dev, CPU_GEN);	
		ulRegRead|=CPU_GEN_SYSTEM_RESET;
		write_nic_dword(dev, CPU_GEN, ulRegRead);
	} else {
		//2008.06.03 for WOL
		write_nic_dword(dev, WFCRC0, 0xffffffff);
		write_nic_dword(dev, WFCRC1, 0xffffffff);
		write_nic_dword(dev, WFCRC2, 0xffffffff);
#ifdef RTL8190P
		//GPIO 0 = TRUE
		ucRegRead = read_nic_byte(dev, GPO);
		ucRegRead |= BIT0;
		write_nic_byte(dev, GPO, ucRegRead);
#endif			
		//Write PMR register
		write_nic_byte(dev, PMR, 0x5);
		//Disable tx, enanble rx
		write_nic_byte(dev, MacBlkCtrl, 0xa);
	}
#endif
out_pci_suspend:
	RT_TRACE(COMP_POWER, "r8192E support WOL call??????????????????????\n");
	printk("r8192E support WOL call??????????????????????\n");
	if(priv->ieee80211->bSupportRemoteWakeUp) {
		RT_TRACE(COMP_POWER, "r8192E support WOL call!!!!!!!!!!!!!!!!!!.\n");
	}
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev,state),\
			priv->ieee80211->bSupportRemoteWakeUp?1:0);
	pci_set_power_state(pdev,pci_choose_state(pdev,state));

        //lzm 090331 for no UI hibernate crash issue
        mdelay(20);

	return 0;
}

int rtl8192E_resume (struct pci_dev *pdev)
{
    struct net_device *dev = pci_get_drvdata(pdev);
    struct r8192_priv *priv = ieee80211_priv(dev); 
    int err;
    u32 val;

    RT_TRACE(COMP_POWER, "================>r8192E resume call.");
    printk("================>r8192E resume call.\n");

    pci_set_power_state(pdev, PCI_D0);

    err = pci_enable_device(pdev);
    if(err) {
        printk(KERN_ERR "%s: pci_enable_device failed on resume\n",
                dev->name);
        return err;
    }

    pci_restore_state(pdev);

    /*
     * Suspend/Resume resets the PCI configuration space, so we have to
     * re-disable the RETRY_TIMEOUT register (0x41) to keep PCI Tx retries
     * from interfering with C3 CPU state. pci_restore_state won't help
     * here since it only restores the first 64 bytes pci config header.
     */
    pci_read_config_dword(pdev, 0x40, &val);
    if ((val & 0x0000ff00) != 0) {
        pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);
    }

    pci_enable_wake(pdev, PCI_D0, 0);

#ifdef ENABLE_GPIO_RADIO_CTL
    if(priv->polling_timer_on == 0){//add for S3/S4
        check_rfctrl_gpio_timer((unsigned long)dev);
    }
#endif

    if(!netif_running(dev)){
        printk("RTL819XE:UI is open out of resume function\n");
        goto out;
    }

    netif_device_attach(dev);

    dev->open(dev);
out:
    RT_TRACE(COMP_POWER, "<================r8192E resume call.\n");
    return 0;
}


int rtl8192E_enable_wake (struct pci_dev *dev, pm_message_t state, int enable)
{
//        printk(KERN_NOTICE "r8192E enable wake call (state %u, enable %d).\n", 
	//       state.event, enable);
	return(-EAGAIN);
}

#endif //CONFIG_PM_RTL

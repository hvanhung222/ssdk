/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "sw.h"
#include "hsl_phy.h"
#include "ssdk_plat.h"
#include "ssdk_mht_pinctrl.h"
#include "qca808x_phy.h"
#include "qca8084_phy.h"
#include "mht_sec_ctrl.h"
#include "mht_interface_ctrl.h"
#include "ssdk_mht_clk.h"

sw_error_t
qca8084_phy_ipg_config(a_uint32_t dev_id, a_uint32_t phy_id,
	fal_port_speed_t speed)
{
	sw_error_t rv = SW_OK;
	a_uint16_t phy_data = 0;

	phy_data = qca808x_phy_mmd_read(dev_id, phy_id, QCA808X_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_IPG_10_11_ENABLE);
	PHY_RTN_ON_READ_ERROR(phy_data);

	phy_data &= ~QCA8084_PHY_MMD7_IPG_11_EN;
	/*If speed is 1G, enable 11 ipg tuning*/
	SSDK_DEBUG("if speed is 1G, enable 11 ipg tuning\n");
	if(speed == FAL_SPEED_1000)
		phy_data |= QCA8084_PHY_MMD7_IPG_11_EN;

	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA808X_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_IPG_10_11_ENABLE, phy_data);

	return rv;
}

static sw_error_t
qca8084_phy_sgmii_mode_set(a_uint32_t dev_id, a_uint32_t phy_addr,
	fal_port_interface_mode_t interface_mode)
{
	sw_error_t rv = SW_OK;
	a_uint32_t phy_addr_tmp = 0;
	fal_mac_config_t config = {0};

	if(interface_mode == PHY_SGMII_BASET)
		config.mac_mode = FAL_MAC_MODE_SGMII;
	else if(interface_mode == PORT_SGMII_PLUS)
		config.mac_mode = FAL_MAC_MODE_SGMII_PLUS;
	else
		return SW_NOT_SUPPORTED;
	config.config.sgmii.clock_mode = FAL_INTERFACE_CLOCK_PHY_MODE;
	config.config.sgmii.auto_neg = A_TRUE;

	rv = qca_mht_ephy_addr_get(dev_id, SSDK_PHYSICAL_PORT4, &phy_addr_tmp);
	SW_RTN_ON_ERROR (rv);
	if(phy_addr_tmp != phy_addr)
	{
		SSDK_ERROR("phy_addr:0x%x is not matched with port4 phy addr:0x%x\n",
			phy_addr, phy_addr_tmp);
		return SW_NOT_SUPPORTED;
	}

	rv = mht_interface_sgmii_mode_set(dev_id, MHT_UNIPHY_SGMII_0,
		SSDK_PHYSICAL_PORT4, &config);
	SW_RTN_ON_ERROR (rv);
	/*port4 software reset*/
	SSDK_INFO("port 4 software reset\n");
	rv = qca808x_phy_reset(dev_id, phy_addr);

	return rv;
}

static sw_error_t
_qca8084_phy_interface_get_mode_status(a_uint32_t dev_id, a_uint32_t phy_id,
	fal_port_interface_mode_t *interface_mode_status)
{
	a_uint32_t mht_port_id = 0;
	fal_port_speed_t speed = 0;
	sw_error_t rv = SW_OK;

	rv = qca_mht_port_id_get(dev_id, phy_id, &mht_port_id);
	SW_RTN_ON_ERROR(rv);

	*interface_mode_status = PORT_INTERFACE_MODE_MAX;

	if(mht_port_id >= SSDK_PHYSICAL_PORT1 && mht_port_id <= SSDK_PHYSICAL_PORT3)
	{
		/*if uniphy1 is UQXGMII mode, then port interface mode is PORT_UQXGMII,
			if not, then port1~port3 is switch port and port interface mode is
			PORT_INTERFACE_MODE_MAX*/
		if(mht_uniphy_mode_check(dev_id, MHT_UNIPHY_SGMII_1, MHT_UNIPHY_UQXGMII))
		{
			*interface_mode_status = PORT_UQXGMII;
		}
	}
	else
	{
		/*if uniphy0 is mac mode, then there are two case: 1. uniphy0 is not
			used(sgmii and mac mode in default), and if uniphy1 is uqxgmii,
			then port4 interface mode is PORT_UQXGMII, 2. uniphy0 is used as
			sgmii or sgmii+, means uniphy0 is used for switch mode, the the
			port4 interface mode is PORT_INTERFACE_MODE_MAX*/
		if(mht_uniphy_mode_check(dev_id, MHT_UNIPHY_SGMII_0, MHT_UNIPHY_MAC)
			&& mht_uniphy_mode_check(dev_id, MHT_UNIPHY_SGMII_1,
			MHT_UNIPHY_UQXGMII))
		{
			*interface_mode_status = PORT_UQXGMII;
		}
		/*if uniphy0 is phy mode, then uniphy0 connect to port4, so the current
			interface mode depend on current speed, sgmii+ for 2.5G and sgmii
			for 1G/100M/10M*/
		if(mht_uniphy_mode_check(dev_id, MHT_UNIPHY_SGMII_0, MHT_UNIPHY_PHY))
		{
			rv = qca808x_phy_get_speed(dev_id, phy_id, &speed);
			SW_RTN_ON_ERROR(rv);
			if(speed == FAL_SPEED_2500)
				*interface_mode_status = PORT_SGMII_PLUS;
			else
				*interface_mode_status = PHY_SGMII_BASET;
		}
	}

	return SW_OK;
}

static sw_error_t
qca8084_phy_interface_mode_fixup(a_uint32_t dev_id, a_uint32_t phy_id,
	fal_port_interface_mode_t mode_new)
{
	sw_error_t rv = SW_OK;
	phy_info_t *phy_info = hsl_phy_info_get(dev_id);
	a_uint32_t port_id = qca_ssdk_phy_addr_to_port(dev_id, phy_id);

	/*only SGMII and SGMII+ may need to change interface mode*/
	if(mode_new != PHY_SGMII_BASET && mode_new != PORT_SGMII_PLUS)
		return SW_OK;

	if(phy_info->port_mode[port_id] != mode_new)
	{
		SSDK_INFO("interface mode change from mode 0x%x to mode 0x%x\n",
			phy_info->port_mode[port_id], mode_new);
		rv = qca8084_phy_sgmii_mode_set(dev_id, phy_id, mode_new);
		SW_RTN_ON_ERROR (rv);
		phy_info->port_mode[port_id] = mode_new;
	}

	return SW_OK;
}

sw_error_t
qca8084_phy_interface_get_mode_status(a_uint32_t dev_id, a_uint32_t phy_id,
	fal_port_interface_mode_t *interface_mode_status)
{
	sw_error_t rv = SW_OK;

	rv = _qca8084_phy_interface_get_mode_status(dev_id, phy_id, interface_mode_status);
	SW_RTN_ON_ERROR (rv);

	rv = qca8084_phy_interface_mode_fixup(dev_id, phy_id, *interface_mode_status);

	return rv;
}

sw_error_t
qca8084_phy_interface_set_mode(a_uint32_t dev_id, a_uint32_t phy_id,
		fal_port_interface_mode_t interface_mode)
{
	sw_error_t rv = SW_OK;
	phy_info_t *phy_info = hsl_phy_info_get(dev_id);
	a_uint32_t port_id  = 0;

	switch (interface_mode) {
		case PORT_UQXGMII:
			SSDK_INFO("configure manhattan phy as PORT_UQXGMII\n");
			rv = qca_mht_mem_ctrl_set(dev_id, MHT_MEM_CTRL_DVS_PHY_MODE,
				MHT_MEM_ACC_0_PHY_MODE);
			SW_RTN_ON_ERROR (rv);
			/*the work mode is PORT_UQXGMII in default*/
			rv = mht_interface_uqxgmii_mode_set(dev_id);
			SW_RTN_ON_ERROR (rv);
			/*init clock for PORT_UQXGMII*/
			ssdk_mht_gcc_clock_init(dev_id, MHT_PHY_UQXGMII_MODE, 0);

			/*init pinctrl for phy mode*/
			rv = ssdk_mht_pinctrl_init(dev_id);

			/*init port mode*/
			for(port_id = SSDK_PHYSICAL_PORT1; port_id <= SSDK_PHYSICAL_PORT4;
				port_id++)
			{
				phy_info->port_mode[port_id] = PORT_UQXGMII;
			}
			break;
		case PHY_SGMII_BASET:
		case PORT_SGMII_PLUS:
			SSDK_INFO("configure manhattan work mode:PHY_SGMII_USXGMII_MODE,"
				"interface_mode:%d\n", interface_mode);
			/*need to configure work mode as MHT_PHY_SGMII_USXGMII_MODE*/
			rv = qca_mht_work_mode_set(dev_id, MHT_PHY_SGMII_UQXGMII_MODE);
			SW_RTN_ON_ERROR (rv);
			rv = qca8084_phy_sgmii_mode_set(dev_id, phy_id, interface_mode);
			SW_RTN_ON_ERROR (rv);
			phy_info->port_mode[qca_ssdk_phy_addr_to_port(dev_id, phy_id)] =
				interface_mode;
			break;
		default:
			rv = SW_NOT_SUPPORTED;
			SSDK_WARN("Unsupport interface mode: %d\n", interface_mode);
			break;
	}

	return rv;
}

sw_error_t
qca8084_phy_set_8023az(a_uint32_t dev_id, a_uint32_t phy_id, a_bool_t enable)
{
	a_uint16_t phy_data;
	sw_error_t rv = SW_OK;

	phy_data = qca808x_phy_mmd_read(dev_id, phy_id, QCA8084_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_ADDR_8023AZ_EEE_2500M_CTRL);
	PHY_RTN_ON_READ_ERROR(phy_data);

	if (enable == A_TRUE) {
		phy_data |= QCA8084_PHY_8023AZ_EEE_2500BT;
	} else {
		phy_data &= ~QCA8084_PHY_8023AZ_EEE_2500BT;
	}

	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_ADDR_8023AZ_EEE_2500M_CTRL, phy_data);

	return rv;
}

sw_error_t
qca8084_phy_get_8023az(a_uint32_t dev_id, a_uint32_t phy_id, a_bool_t * enable)
{
	a_uint16_t phy_data;

	*enable = A_FALSE;

	phy_data = qca808x_phy_mmd_read(dev_id, phy_id, QCA8084_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_ADDR_8023AZ_EEE_2500M_CTRL);
	PHY_RTN_ON_READ_ERROR(phy_data);

	if (phy_data & QCA8084_PHY_8023AZ_EEE_2500BT) {
		*enable = A_TRUE;
	}

	return SW_OK;
}

sw_error_t
qca8084_phy_set_eee_adv(a_uint32_t dev_id, a_uint32_t phy_id,
	a_uint32_t adv)
{
	a_uint16_t phy_data = 0;
	sw_error_t rv = SW_OK;

	phy_data = qca808x_phy_mmd_read(dev_id, phy_id, QCA8084_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_ADDR_8023AZ_EEE_2500M_CTRL);
	PHY_RTN_ON_READ_ERROR(phy_data);
	phy_data &= ~(QCA8084_PHY_8023AZ_EEE_2500BT);

	if (adv & FAL_PHY_EEE_2500BASE_T) {
		phy_data |= QCA8084_PHY_8023AZ_EEE_2500BT;
	}

	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_ADDR_8023AZ_EEE_2500M_CTRL, phy_data);

	return rv;
}

sw_error_t
qca8084_phy_get_eee_adv(a_uint32_t dev_id, a_uint32_t phy_id,
	a_uint32_t *adv)
{
	a_uint16_t phy_data = 0;

	phy_data = qca808x_phy_mmd_read(dev_id, phy_id, QCA8084_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_ADDR_8023AZ_EEE_2500M_CTRL);
	PHY_RTN_ON_READ_ERROR(phy_data);

	if (phy_data & QCA8084_PHY_8023AZ_EEE_2500BT) {
		*adv |= FAL_PHY_EEE_2500BASE_T;
	}

	return SW_OK;
}

sw_error_t
qca8084_phy_get_eee_partner_adv(a_uint32_t dev_id, a_uint32_t phy_id,
	a_uint32_t *adv)
{
	a_uint16_t phy_data = 0;

	phy_data = qca808x_phy_mmd_read(dev_id, phy_id, QCA8084_PHY_MMD7_NUM,
		QCA8084_PHY_MMD7_ADDR_8023AZ_EEE_2500M_PARTNER);
	PHY_RTN_ON_READ_ERROR(phy_data);

	if (phy_data & QCA8084_PHY_8023AZ_EEE_2500BT) {
		*adv |= FAL_PHY_EEE_2500BASE_T;
	}

	return SW_OK;
}

sw_error_t
qca8084_phy_get_eee_cap(a_uint32_t dev_id, a_uint32_t phy_id,
	a_uint32_t *cap)
{
	a_uint16_t phy_data = 0;

	phy_data = qca808x_phy_mmd_read(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_ADDR_8023AZ_EEE_2500M_CAPABILITY);

	if (phy_data & QCA8084_PHY_EEE_CAPABILITY_2500M) {
		*cap |= FAL_PHY_EEE_2500BASE_T;
	}

	return SW_OK;
}

sw_error_t
qca8084_phy_get_eee_status(a_uint32_t dev_id, a_uint32_t phy_id,
	a_uint32_t *status)
{
	a_uint32_t adv = 0, lp_adv = 0;
	sw_error_t rv = SW_OK;

	rv = qca8084_phy_get_eee_adv(dev_id, phy_id, &adv);
	SW_RTN_ON_ERROR(rv);

	rv = qca8084_phy_get_eee_partner_adv(dev_id, phy_id, &lp_adv);
	SW_RTN_ON_ERROR(rv);

	*status |= (adv & lp_adv);

	return SW_OK;
}

static sw_error_t
qca8084_phy_cdt_thresh_init(a_uint32_t dev_id, a_uint32_t phy_id)
{
	sw_error_t rv = SW_OK;

	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL3,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL3_VAL);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL4,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL4_VAL);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL5,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL5_VAL);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL6,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL6_VAL);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL7,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL7_VAL);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL9,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL9_VAL);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL13,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL13_VAL);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_mmd_write(dev_id, phy_id, QCA8084_PHY_MMD3_NUM,
		QCA8084_PHY_MMD3_CDT_THRESH_CTRL14,
		QCA8084_PHY_MMD3_NEAR_ECHO_THRESH_VAL);

	return rv;
}

static sw_error_t
qca8084_phy_function_reset(a_uint32_t dev_id, a_uint32_t phy_id,
	hsl_phy_function_reset_t phy_reset_type)
{
	a_uint16_t phy_data = 0;
	sw_error_t rv = SW_OK;

	phy_data = qca808x_phy_reg_read (dev_id, phy_id, QCA8084_PHY_FIFO_CONTROL);
	PHY_RTN_ON_READ_ERROR(phy_data);

	rv = qca808x_phy_reg_write(dev_id, phy_id, QCA8084_PHY_FIFO_CONTROL,
		phy_data & (~QCA8084_PHY_FIFO_RESET));
	SW_RTN_ON_ERROR(rv);

	aos_mdelay(50);

	rv = qca808x_phy_reg_write(dev_id, phy_id, QCA8084_PHY_FIFO_CONTROL,
	phy_data | QCA8084_PHY_FIFO_RESET);
	SW_RTN_ON_ERROR(rv);

	return rv;
}

static sw_error_t
_qca8084_phy_uqxgmii_speed_fixup(a_uint32_t dev_id, a_uint32_t phy_addr,
	a_uint32_t link, fal_port_speed_t new_speed)
{
	sw_error_t rv = SW_OK;
	a_uint32_t mht_port_id = 0;
	a_bool_t port_clock_en = A_FALSE;

	rv = qca_mht_port_id_get(dev_id, phy_addr, &mht_port_id);
	SW_RTN_ON_ERROR(rv);

	/*Restart the auto-neg of uniphy*/
	SSDK_DEBUG("Restart the auto-neg of uniphy\n");
	rv = mht_uniphy_xpcs_autoneg_restart(dev_id, mht_port_id);
	SW_RTN_ON_ERROR(rv);
	/*set gmii+ clock to uniphy1 and ethphy*/
	SSDK_DEBUG("set gmii,xgmii clock to uniphy and gmii to ethphy\n");
	rv = mht_port_speed_clock_set(dev_id, mht_port_id, new_speed);
	SW_RTN_ON_ERROR(rv);
	/*set xpcs speed*/
	SSDK_DEBUG("set xpcs speed\n");
	rv = mht_uniphy_xpcs_speed_set(dev_id, mht_port_id, new_speed);
	SW_RTN_ON_ERROR(rv);

	/*GMII/XGMII clock and ETHPHY GMII clock enable/disable*/
	SSDK_DEBUG("GMII/XGMII clock and ETHPHY GMII clock enable/disable\n");
	if(link)
		port_clock_en = A_TRUE;
	rv = ssdk_mht_port_clk_en_set(dev_id, mht_port_id,
		MHT_CLK_TYPE_UNIPHY|MHT_CLK_TYPE_EPHY, port_clock_en);
	SW_RTN_ON_ERROR(rv);
	/*GMII/XGMII interface and ETHPHY GMII interface reset and release*/
	SSDK_DEBUG("UNIPHY GMII/XGMII interface and ETHPHY GMII interface reset and release\n");
	rv = ssdk_mht_port_clk_reset(dev_id, mht_port_id, MHT_CLK_TYPE_UNIPHY|MHT_CLK_TYPE_EPHY);
	SW_RTN_ON_ERROR(rv);
	/*ipg_tune and xgmii2gmii reset for uniphy and ETHPHY, function reset*/
	SSDK_DEBUG("ipg_tune and xgmii2gmii reset for uniphy and ETHPHY, function reset\n");
	rv = mht_uniphy_uqxgmii_function_reset(dev_id, mht_port_id);
	SW_RTN_ON_ERROR(rv);
	/*do ethphy function reset*/
	SSDK_DEBUG("do ethphy function reset\n");
	rv = qca8084_phy_function_reset(dev_id, phy_addr, PHY_FIFO_RESET);
	SW_RTN_ON_ERROR(rv);
	/*change IPG from 10 to 11 for 1G speed*/
	rv = qca8084_phy_ipg_config(dev_id, phy_addr, new_speed);

	return rv;
}

static sw_error_t
_qca8084_phy_sgmii_speed_fixup (a_uint32_t dev_id, a_uint32_t phy_addr,
	a_uint32_t link, fal_port_speed_t new_speed)
{
	sw_error_t rv = SW_OK;

	/*disable ethphy3 and uniphy0 clock*/
	SSDK_INFO("disable ethphy3 and uniphy0 clock\n");
	rv = ssdk_mht_port_clk_en_set(dev_id, SSDK_PHYSICAL_PORT4,
		MHT_CLK_TYPE_EPHY, A_FALSE);
	SW_RTN_ON_ERROR(rv);
	rv = ssdk_mht_port_clk_en_set(dev_id, SSDK_PHYSICAL_PORT5,
		MHT_CLK_TYPE_UNIPHY, A_FALSE);
	SW_RTN_ON_ERROR(rv);
	/*set gmii clock for ethphy3 and uniphy0*/
	SSDK_INFO("set speed clock for uniphy3 and uniphy0\n");
	rv = mht_port_speed_clock_set(dev_id, SSDK_PHYSICAL_PORT4, new_speed);
	SW_RTN_ON_ERROR(rv);
	/*uniphy and ethphy gmii clock enable/disable*/
	SSDK_INFO("uniphy and ethphy GMII clock enable/disable\n");
	if(link)
	{
		SSDK_INFO("enable ethphy3 and uniphy0 clock\n");
		rv = ssdk_mht_port_clk_en_set(dev_id, SSDK_PHYSICAL_PORT4,
			MHT_CLK_TYPE_EPHY, A_TRUE);
		SW_RTN_ON_ERROR(rv);
		rv = ssdk_mht_port_clk_en_set(dev_id, SSDK_PHYSICAL_PORT5,
			MHT_CLK_TYPE_UNIPHY, A_TRUE);
		SW_RTN_ON_ERROR(rv);
	}
	/*uniphy and ethphy gmii reset and release*/
	SSDK_INFO("uniphy and ethphy GMII interface reset and release\n");
	rv = ssdk_mht_port_clk_reset(dev_id, SSDK_PHYSICAL_PORT4,
		MHT_CLK_TYPE_EPHY);
	SW_RTN_ON_ERROR(rv);
	rv = ssdk_mht_port_clk_reset(dev_id, SSDK_PHYSICAL_PORT5,
		MHT_CLK_TYPE_UNIPHY);
	SW_RTN_ON_ERROR(rv);
	/*uniphy and ethphy ipg_tune reset, function reset*/
	SSDK_INFO("uniphy and ethphy ipg_tune reset, function reset\n");
	rv = mht_uniphy_sgmii_function_reset(dev_id, MHT_UNIPHY_SGMII_0);
	SW_RTN_ON_ERROR(rv);
	/*do ethphy function reset*/
	SSDK_INFO("do ethphy function reset\n");
	rv = qca8084_phy_function_reset(dev_id, phy_addr, PHY_FIFO_RESET);

	return rv;
}

static sw_error_t
_qca8084_phy_speed_fixup (a_uint32_t dev_id, a_uint32_t phy_addr,
	a_uint32_t link, fal_port_interface_mode_t mode, fal_port_speed_t new_speed)
{
	sw_error_t rv = SW_OK;

	if(mode == PORT_SGMII_PLUS || mode == PHY_SGMII_BASET)
	{
		rv = _qca8084_phy_sgmii_speed_fixup(dev_id, phy_addr, link,
			new_speed);
		SW_RTN_ON_ERROR(rv);
	}
	else if(mode == PORT_UQXGMII)
	{
		rv = _qca8084_phy_uqxgmii_speed_fixup(dev_id, phy_addr, link,
			new_speed);
		SW_RTN_ON_ERROR(rv);
	}

	return SW_OK;
}

sw_error_t
qca8084_phy_speed_fixup(a_uint32_t dev_id, a_uint32_t phy_addr,
	struct port_phy_status *phy_status)
{
	a_uint32_t port_id = 0;
	phy_info_t *phy_info = hsl_phy_info_get(dev_id);

	port_id = qca_ssdk_phy_addr_to_port(dev_id, phy_addr);
	if(phy_info->port_link_status[port_id] != phy_status->link_status)
	{
		SSDK_DEBUG("port %d link status is from %s to %s, speed is:%d\n",
			port_id,
			phy_info->port_link_status[port_id] ? "link up" :"link down",
			phy_status->link_status ? "link up" : "link down",
			phy_status->speed);
		_qca8084_phy_speed_fixup(dev_id, phy_addr, phy_status->link_status,
			phy_info->port_mode[port_id], phy_status->speed);
		phy_info->port_link_status[port_id] = phy_status->link_status;
	}

	return SW_OK;
}

static sw_error_t
qca8084_phy_adc_edge_set(a_uint32_t dev_id, a_uint32_t phy_addr,
	qca8084_adc_edge_t adc_edge)
{
	sw_error_t rv = SW_OK;

	if(adc_edge != ADC_RISING && adc_edge != ADC_FALLING)
		return SW_NOT_SUPPORTED;

	rv = qca808x_phy_modify_debug(dev_id, phy_addr,
		QCA8084_PHY_DEBUG_ANA_INTERFACE_CLK_SEL, BITS(4,4), adc_edge);
	SW_RTN_ON_ERROR(rv);
	rv = qca808x_phy_reset(dev_id, phy_addr);

	return rv;
}

void
qca8084_phy_api_ops_init(hsl_phy_ops_t * hsl_phy_ops)
{
	if(hsl_phy_ops == NULL)
	{
		SSDK_ERROR ("hsl_phy_ops is null\n");
		return;
	}
	hsl_phy_ops->phy_function_reset = qca8084_phy_function_reset;
}

sw_error_t
qca8084_phy_hw_init(a_uint32_t dev_id, a_uint32_t phy_addr)
{
	sw_error_t rv = SW_OK;

	/*adjust CDT threshold*/
	rv = qca8084_phy_cdt_thresh_init(dev_id, phy_addr);
	SW_RTN_ON_ERROR(rv);
	/*invert ADC clock edge as falling edge to fix link issue*/
	rv = qca8084_phy_adc_edge_set(dev_id, phy_addr, ADC_FALLING);

	return rv;
}

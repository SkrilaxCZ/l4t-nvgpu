/*
 * Copyright (c) 2016-2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <nvgpu/bios.h>

#include "gk20a/gk20a.h"
#include "clk.h"
#include "clk_fll.h"
#include "clk_domain.h"
#include "boardobj/boardobjgrp.h"
#include "boardobj/boardobjgrp_e32.h"
#include "ctrl/ctrlclk.h"
#include "ctrl/ctrlvolt.h"

static u32 devinit_get_fll_device_table(struct gk20a *g,
				   struct avfsfllobjs *pfllobjs);
static struct fll_device *construct_fll_device(struct gk20a *g,
		void *pargs);
static u32 fll_device_init_pmudata_super(struct gk20a *g,
				    struct boardobj *board_obj_ptr,
				    struct nv_pmu_boardobj *ppmudata);

static u32 _clk_fll_devgrp_pmudatainit_super(struct gk20a *g,
					       struct boardobjgrp *pboardobjgrp,
					       struct nv_pmu_boardobjgrp_super *pboardobjgrppmu)
{
	struct nv_pmu_clk_clk_fll_device_boardobjgrp_set_header *pset =
		(struct nv_pmu_clk_clk_fll_device_boardobjgrp_set_header *)
		pboardobjgrppmu;
	struct avfsfllobjs *pfll_objs = (struct avfsfllobjs *)
		pboardobjgrp;
	u32 status = 0;

	nvgpu_log_info(g, " ");

	status = boardobjgrp_pmudatainit_e32(g, pboardobjgrp, pboardobjgrppmu);
	if (status) {
		nvgpu_err(g, "failed to init fll pmuobjgrp");
		return status;
	}
	pset->lut_num_entries = pfll_objs->lut_num_entries;
	pset->lut_step_size_uv = pfll_objs->lut_step_size_uv;
	pset->lut_min_voltage_uv = pfll_objs->lut_min_voltage_uv;
	pset->max_min_freq_mhz = pfll_objs->max_min_freq_mhz;

	status = boardobjgrpmask_export(
		&pfll_objs->lut_prog_master_mask.super,
		pfll_objs->lut_prog_master_mask.super.bitcount,
		&pset->lut_prog_master_mask.super);

	nvgpu_log_info(g, " Done");
	return status;
}

static u32 _clk_fll_devgrp_pmudata_instget(struct gk20a *g,
					     struct nv_pmu_boardobjgrp *pmuboardobjgrp,
					     struct nv_pmu_boardobj **ppboardobjpmudata,
					     u8 idx)
{
	struct nv_pmu_clk_clk_fll_device_boardobj_grp_set  *pgrp_set =
		(struct nv_pmu_clk_clk_fll_device_boardobj_grp_set *)
		pmuboardobjgrp;

	nvgpu_log_info(g, " ");

	/*check whether pmuboardobjgrp has a valid boardobj in index*/
	if (((u32)BIT(idx) &
		pgrp_set->hdr.data.super.obj_mask.super.data[0]) == 0)
		return -EINVAL;

	*ppboardobjpmudata = (struct nv_pmu_boardobj *)
		&pgrp_set->objects[idx].data.board_obj;
	nvgpu_log_info(g, " Done");
	return 0;
}

static u32 _clk_fll_devgrp_pmustatus_instget(struct gk20a *g,
					       void *pboardobjgrppmu,
					       struct nv_pmu_boardobj_query **ppboardobjpmustatus,
					       u8 idx)
{
	struct nv_pmu_clk_clk_fll_device_boardobj_grp_get_status *pgrp_get_status =
		(struct nv_pmu_clk_clk_fll_device_boardobj_grp_get_status *)
		pboardobjgrppmu;

	/*check whether pmuboardobjgrp has a valid boardobj in index*/
	if (((u32)BIT(idx) &
		pgrp_get_status->hdr.data.super.obj_mask.super.data[0]) == 0)
		return -EINVAL;

	*ppboardobjpmustatus = (struct nv_pmu_boardobj_query *)
			&pgrp_get_status->objects[idx].data.board_obj;
	return 0;
}

u32 clk_fll_sw_setup(struct gk20a *g)
{
	u32 status;
	struct boardobjgrp *pboardobjgrp = NULL;
	struct avfsfllobjs *pfllobjs;
	struct fll_device *pfll;
	struct fll_device *pfll_master;
	struct fll_device *pfll_local;
	u8 i;
	u8 j;

	nvgpu_log_info(g, " ");

	status = boardobjgrpconstruct_e32(g, &g->clk_pmu.avfs_fllobjs.super);
	if (status) {
		nvgpu_err(g,
		"error creating boardobjgrp for fll, status - 0x%x", status);
		goto done;
	}
	pfllobjs = &(g->clk_pmu.avfs_fllobjs);
	pboardobjgrp = &(g->clk_pmu.avfs_fllobjs.super.super);

	BOARDOBJGRP_PMU_CONSTRUCT(pboardobjgrp, CLK, FLL_DEVICE);

	status = BOARDOBJGRP_PMU_CMD_GRP_SET_CONSTRUCT(g, pboardobjgrp,
			clk, CLK, clk_fll_device, CLK_FLL_DEVICE);
	if (status) {
		nvgpu_err(g,
			  "error constructing PMU_BOARDOBJ_CMD_GRP_SET interface - 0x%x",
			  status);
		goto done;
	}

	pboardobjgrp->pmudatainit  = _clk_fll_devgrp_pmudatainit_super;
	pboardobjgrp->pmudatainstget  = _clk_fll_devgrp_pmudata_instget;
	pboardobjgrp->pmustatusinstget  = _clk_fll_devgrp_pmustatus_instget;
	pfllobjs = (struct avfsfllobjs *)pboardobjgrp;
	pfllobjs->lut_num_entries = CTRL_CLK_LUT_NUM_ENTRIES;
	pfllobjs->lut_step_size_uv = CTRL_CLK_VIN_STEP_SIZE_UV;
	pfllobjs->lut_min_voltage_uv = CTRL_CLK_LUT_MIN_VOLTAGE_UV;

	/* Initialize lut prog master mask to zero.*/
	boardobjgrpmask_e32_init(&pfllobjs->lut_prog_master_mask, NULL);

	status = devinit_get_fll_device_table(g, pfllobjs);
	if (status)
		goto done;

	status = BOARDOBJGRP_PMU_CMD_GRP_GET_STATUS_CONSTRUCT(g,
				&g->clk_pmu.avfs_fllobjs.super.super,
				clk, CLK, clk_fll_device, CLK_FLL_DEVICE);
	if (status) {
		nvgpu_err(g,
			  "error constructing PMU_BOARDOBJ_CMD_GRP_SET interface - 0x%x",
			  status);
		goto done;
	}

	BOARDOBJGRP_FOR_EACH(&(pfllobjs->super.super),
			     struct fll_device *, pfll, i) {
		pfll_master = NULL;
		j = 0;
		BOARDOBJGRP_ITERATOR(&(pfllobjs->super.super),
				     struct fll_device *, pfll_local, j,
				     &pfllobjs->lut_prog_master_mask.super) {
			if (pfll_local->clk_domain == pfll->clk_domain) {
				pfll_master = pfll_local;
				break;
			}
		}

		if (pfll_master == NULL) {
			status = boardobjgrpmask_bitset(
				&pfllobjs->lut_prog_master_mask.super,
				BOARDOBJ_GET_IDX(pfll));
			if (status) {
				nvgpu_err(g, "err setting lutprogmask");
				goto done;
			}
			pfll_master = pfll;
		}
		status = pfll_master->lut_broadcast_slave_register(
			g, pfllobjs, pfll_master, pfll);

		if (status) {
			nvgpu_err(g, "err setting lutslavemask");
			goto done;
		}
	}
done:
	nvgpu_log_info(g, " done status %x", status);
	return status;
}

u32 clk_fll_pmu_setup(struct gk20a *g)
{
	u32 status;
	struct boardobjgrp *pboardobjgrp = NULL;

	nvgpu_log_info(g, " ");

	pboardobjgrp = &g->clk_pmu.avfs_fllobjs.super.super;

	if (!pboardobjgrp->bconstructed)
		return -EINVAL;

	status = pboardobjgrp->pmuinithandle(g, pboardobjgrp);

	nvgpu_log_info(g, "Done");
	return status;
}

static u32 devinit_get_fll_device_table(struct gk20a *g,
				   struct avfsfllobjs *pfllobjs)
{
	u32 status = 0;
	u8 *fll_table_ptr = NULL;
	struct fll_descriptor_header fll_desc_table_header_sz = { 0 };
	struct fll_descriptor_header_10 fll_desc_table_header = { 0 };
	struct fll_descriptor_entry_10 fll_desc_table_entry = { 0 };
	u8 *fll_tbl_entry_ptr = NULL;
	u32 index = 0;
	struct fll_device fll_dev_data;
	struct fll_device *pfll_dev;
	struct vin_device *pvin_dev;
	u32 desctablesize;
	u32 vbios_domain = NV_PERF_DOMAIN_4X_CLOCK_DOMAIN_SKIP;
	struct avfsvinobjs *pvinobjs = &g->clk_pmu.avfs_vinobjs;

	nvgpu_log_info(g, " ");

	fll_table_ptr = (u8 *)nvgpu_bios_get_perf_table_ptrs(g,
			  g->bios.clock_token, FLL_TABLE);
	if (fll_table_ptr == NULL) {
		status = -1;
		goto done;
	}

	memcpy(&fll_desc_table_header_sz, fll_table_ptr,
			sizeof(struct fll_descriptor_header));
	if (fll_desc_table_header_sz.size >= FLL_DESCRIPTOR_HEADER_10_SIZE_6)
		desctablesize = FLL_DESCRIPTOR_HEADER_10_SIZE_6;
	else
		desctablesize = FLL_DESCRIPTOR_HEADER_10_SIZE_4;

	memcpy(&fll_desc_table_header, fll_table_ptr, desctablesize);

	if (desctablesize == FLL_DESCRIPTOR_HEADER_10_SIZE_6)
		pfllobjs->max_min_freq_mhz =
			fll_desc_table_header.max_min_freq_mhz;
	else
		pfllobjs->max_min_freq_mhz = 0;

	/* Read table entries*/
	fll_tbl_entry_ptr = fll_table_ptr + desctablesize;
	for (index = 0; index < fll_desc_table_header.entry_count; index++) {
		u32 fll_id;

		memcpy(&fll_desc_table_entry, fll_tbl_entry_ptr,
				sizeof(struct fll_descriptor_entry_10));

		if (fll_desc_table_entry.fll_device_type == CTRL_CLK_FLL_TYPE_DISABLED)
			continue;

		fll_id = fll_desc_table_entry.fll_device_id;

		if ( (u8)fll_desc_table_entry.vin_idx_logic != CTRL_CLK_VIN_ID_UNDEFINED) {
			pvin_dev = CLK_GET_VIN_DEVICE(pvinobjs,
					(u8)fll_desc_table_entry.vin_idx_logic);
			if (pvin_dev == NULL)
				return -EINVAL;
			else
				pvin_dev->flls_shared_mask |= BIT(fll_id);
		} else {
			/* Return if Logic ADC device index is invalid*/
			nvgpu_err(g, "Invalid Logic ADC specified for Nafll ID");
			return -EINVAL;
		}

		fll_dev_data.lut_device.vselect_mode =
			(u8)BIOS_GET_FIELD(fll_desc_table_entry.lut_params,
			NV_FLL_DESC_LUT_PARAMS_VSELECT);

		if ( (u8)fll_desc_table_entry.vin_idx_sram != CTRL_CLK_VIN_ID_UNDEFINED) {
			pvin_dev = CLK_GET_VIN_DEVICE(pvinobjs,
					(u8)fll_desc_table_entry.vin_idx_sram);
			if (pvin_dev == NULL)
				return -EINVAL;
			else
				pvin_dev->flls_shared_mask |= BIT(fll_id);
		} else {
			/* Make sure VSELECT mode is set correctly to _LOGIC*/
			if (fll_dev_data.lut_device.vselect_mode != CTRL_CLK_FLL_LUT_VSELECT_LOGIC)
				return -EINVAL;
		}

		fll_dev_data.super.type =
			(u8)fll_desc_table_entry.fll_device_type;
		fll_dev_data.id = (u8)fll_desc_table_entry.fll_device_id;
		fll_dev_data.mdiv = (u8)BIOS_GET_FIELD(
			fll_desc_table_entry.fll_params,
			NV_FLL_DESC_FLL_PARAMS_MDIV);
		fll_dev_data.input_freq_mhz =
			(u16)fll_desc_table_entry.ref_freq_mhz;
		fll_dev_data.min_freq_vfe_idx =
			(u8)fll_desc_table_entry.min_freq_vfe_idx;
		fll_dev_data.freq_ctrl_idx = CTRL_BOARDOBJ_IDX_INVALID;

		vbios_domain = (u32)(fll_desc_table_entry.clk_domain &
					NV_PERF_DOMAIN_4X_CLOCK_DOMAIN_MASK);
		fll_dev_data.clk_domain =
			g->ops.pmu_ver.clk.get_vbios_clk_domain(vbios_domain);

		fll_dev_data.rail_idx_for_lut = 0;
		fll_dev_data.vin_idx_logic =
			(u8)fll_desc_table_entry.vin_idx_logic;
		fll_dev_data.vin_idx_sram =
			(u8)fll_desc_table_entry.vin_idx_sram;
		fll_dev_data.b_skip_pldiv_below_dvco_min =
			(bool)BIOS_GET_FIELD(fll_desc_table_entry.fll_params,
			NV_FLL_DESC_FLL_PARAMS_SKIP_PLDIV_BELOW_DVCO_MIN);
		fll_dev_data.lut_device.hysteresis_threshold =
			(u8)BIOS_GET_FIELD(fll_desc_table_entry.lut_params,
					   NV_FLL_DESC_LUT_PARAMS_HYSTERISIS_THRESHOLD);
		fll_dev_data.regime_desc.regime_id =
			CTRL_CLK_FLL_REGIME_ID_FFR;
		fll_dev_data.regime_desc.fixed_freq_regime_limit_mhz =
			(u16)fll_desc_table_entry.ffr_cutoff_freq_mhz;
		fll_dev_data.regime_desc.target_regime_id_override=0;

		/*construct fll device*/
		pfll_dev = construct_fll_device(g, (void *)&fll_dev_data);

		status = boardobjgrp_objinsert(&pfllobjs->super.super,
				(struct boardobj *)pfll_dev, index);
		fll_tbl_entry_ptr += fll_desc_table_header.entry_size;
	}

done:
	nvgpu_log_info(g, " done status %x", status);
	return status;
}

u32 nvgpu_clk_get_vbios_clk_domain_gv10x( u32 vbios_domain)
{
	if (vbios_domain == 0)
		return CTRL_CLK_DOMAIN_GPCCLK;
	else if (vbios_domain == 1)
		return CTRL_CLK_DOMAIN_XBARCLK;
	else if (vbios_domain == 3)
		return CTRL_CLK_DOMAIN_SYSCLK;
	else if (vbios_domain == 5)
		return CTRL_CLK_DOMAIN_NVDCLK;
	return 0;
}

u32 nvgpu_clk_get_vbios_clk_domain_gp10x( u32 vbios_domain)
{
	if (vbios_domain == 0)
		return CTRL_CLK_DOMAIN_GPC2CLK;
	else if (vbios_domain == 1)
		return CTRL_CLK_DOMAIN_XBAR2CLK;
	else if (vbios_domain == 3)
		return CTRL_CLK_DOMAIN_SYS2CLK;
	return 0;
}

static u32 lutbroadcastslaveregister(struct gk20a *g,
				     struct avfsfllobjs *pfllobjs,
				     struct fll_device *pfll,
				     struct fll_device *pfll_slave)
{
	if (pfll->clk_domain != pfll_slave->clk_domain)
		return -EINVAL;

	return boardobjgrpmask_bitset(&pfll->
		lut_prog_broadcast_slave_mask.super,
		BOARDOBJ_GET_IDX(pfll_slave));
}

static struct fll_device *construct_fll_device(struct gk20a *g,
		void *pargs)
{
	struct boardobj *board_obj_ptr = NULL;
	struct fll_device *pfll_dev;
	struct fll_device *board_obj_fll_ptr = NULL;
	u32 status;

	nvgpu_log_info(g, " ");
	status = boardobj_construct_super(g, &board_obj_ptr,
		sizeof(struct fll_device), pargs);
	if (status)
		return NULL;

	pfll_dev = (struct fll_device *)pargs;
	board_obj_fll_ptr = (struct fll_device *)board_obj_ptr;
	board_obj_ptr->pmudatainit  = fll_device_init_pmudata_super;
	board_obj_fll_ptr->lut_broadcast_slave_register =
		lutbroadcastslaveregister;
	board_obj_fll_ptr->id = pfll_dev->id;
	board_obj_fll_ptr->mdiv = pfll_dev->mdiv;
	board_obj_fll_ptr->rail_idx_for_lut = pfll_dev->rail_idx_for_lut;
	board_obj_fll_ptr->input_freq_mhz = pfll_dev->input_freq_mhz;
	board_obj_fll_ptr->clk_domain = pfll_dev->clk_domain;
	board_obj_fll_ptr->vin_idx_logic = pfll_dev->vin_idx_logic;
	board_obj_fll_ptr->vin_idx_sram = pfll_dev->vin_idx_sram;
	board_obj_fll_ptr->min_freq_vfe_idx =
		pfll_dev->min_freq_vfe_idx;
	board_obj_fll_ptr->freq_ctrl_idx = pfll_dev->freq_ctrl_idx;
	board_obj_fll_ptr->b_skip_pldiv_below_dvco_min =
		pfll_dev->b_skip_pldiv_below_dvco_min;
	memcpy(&board_obj_fll_ptr->lut_device, &pfll_dev->lut_device,
		sizeof(struct nv_pmu_clk_lut_device_desc));
	memcpy(&board_obj_fll_ptr->regime_desc, &pfll_dev->regime_desc,
		sizeof(struct nv_pmu_clk_regime_desc));
	boardobjgrpmask_e32_init(
		&board_obj_fll_ptr->lut_prog_broadcast_slave_mask, NULL);

	nvgpu_log_info(g, " Done");

	return (struct fll_device *)board_obj_ptr;
}

static u32 fll_device_init_pmudata_super(struct gk20a *g,
				    struct boardobj *board_obj_ptr,
				    struct nv_pmu_boardobj *ppmudata)
{
	u32 status = 0;
	struct fll_device *pfll_dev;
	struct nv_pmu_clk_clk_fll_device_boardobj_set *perf_pmu_data;

	nvgpu_log_info(g, " ");

	status = boardobj_pmudatainit_super(g, board_obj_ptr, ppmudata);
	if (status != 0)
		return status;

	pfll_dev = (struct fll_device *)board_obj_ptr;
	perf_pmu_data = (struct nv_pmu_clk_clk_fll_device_boardobj_set *)
		ppmudata;

	perf_pmu_data->id = pfll_dev->id;
	perf_pmu_data->mdiv = pfll_dev->mdiv;
	perf_pmu_data->rail_idx_for_lut = pfll_dev->rail_idx_for_lut;
	perf_pmu_data->input_freq_mhz = pfll_dev->input_freq_mhz;
	perf_pmu_data->vin_idx_logic = pfll_dev->vin_idx_logic;
	perf_pmu_data->vin_idx_sram = pfll_dev->vin_idx_sram;
	perf_pmu_data->clk_domain = pfll_dev->clk_domain;
	perf_pmu_data->min_freq_vfe_idx =
		pfll_dev->min_freq_vfe_idx;
	perf_pmu_data->freq_ctrl_idx = pfll_dev->freq_ctrl_idx;
	perf_pmu_data->b_skip_pldiv_below_dvco_min = pfll_dev->b_skip_pldiv_below_dvco_min;
	memcpy(&perf_pmu_data->lut_device, &pfll_dev->lut_device,
		sizeof(struct nv_pmu_clk_lut_device_desc));
	memcpy(&perf_pmu_data->regime_desc, &pfll_dev->regime_desc,
		sizeof(struct nv_pmu_clk_regime_desc));

	status = boardobjgrpmask_export(
		&pfll_dev->lut_prog_broadcast_slave_mask.super,
		pfll_dev->lut_prog_broadcast_slave_mask.super.bitcount,
		&perf_pmu_data->lut_prog_broadcast_slave_mask.super);

	nvgpu_log_info(g, " Done");

	return status;
}

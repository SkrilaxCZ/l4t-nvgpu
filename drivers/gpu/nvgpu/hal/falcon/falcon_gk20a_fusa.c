/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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
#include <nvgpu/io.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/falcon.h>
#include <nvgpu/string.h>
#include <nvgpu/static_analysis.h>

#include "falcon_gk20a.h"

#include <nvgpu/hw/gm20b/hw_falcon_gm20b.h>

static inline u32 gk20a_falcon_readl(struct nvgpu_falcon *flcn, u32 offset)
{
	return nvgpu_readl(flcn->g,
			   nvgpu_safe_add_u32(flcn->flcn_base, offset));
}

static inline void gk20a_falcon_writel(struct nvgpu_falcon *flcn,
				       u32 offset, u32 val)
{
	nvgpu_writel(flcn->g, nvgpu_safe_add_u32(flcn->flcn_base, offset), val);
}

void gk20a_falcon_reset(struct nvgpu_falcon *flcn)
{
	u32 unit_status = 0U;

	/* do falcon CPU hard reset */
	unit_status = gk20a_falcon_readl(flcn, falcon_falcon_cpuctl_r());
	gk20a_falcon_writel(flcn, falcon_falcon_cpuctl_r(),
			    (unit_status | falcon_falcon_cpuctl_hreset_f(1)));
}

bool gk20a_is_falcon_cpu_halted(struct nvgpu_falcon *flcn)
{
	return ((gk20a_falcon_readl(flcn, falcon_falcon_cpuctl_r()) &
			falcon_falcon_cpuctl_halt_intr_m()) != 0U);
}

bool gk20a_is_falcon_idle(struct nvgpu_falcon *flcn)
{
	u32 unit_status = 0U;
	bool status = false;

	unit_status = gk20a_falcon_readl(flcn, falcon_falcon_idlestate_r());

	if (falcon_falcon_idlestate_falcon_busy_v(unit_status) == 0U &&
		falcon_falcon_idlestate_ext_busy_v(unit_status) == 0U) {
		status = true;
	} else {
		status = false;
	}

	return status;
}

bool gk20a_is_falcon_scrubbing_done(struct nvgpu_falcon *flcn)
{
	u32 unit_status = 0U;
	bool status = false;

	unit_status = gk20a_falcon_readl(flcn, falcon_falcon_dmactl_r());

	if ((unit_status &
		(falcon_falcon_dmactl_dmem_scrubbing_m() |
		 falcon_falcon_dmactl_imem_scrubbing_m())) != 0U) {
		status = false;
	} else {
		status = true;
	}

	return status;
}

u32 gk20a_falcon_get_mem_size(struct nvgpu_falcon *flcn,
		enum falcon_mem_type mem_type)
{
	u32 mem_size = 0U;
	u32 hwcfg_val = 0U;

	hwcfg_val = gk20a_falcon_readl(flcn, falcon_falcon_hwcfg_r());

	if (mem_type == MEM_DMEM) {
		mem_size = falcon_falcon_hwcfg_dmem_size_v(hwcfg_val)
			<< FALCON_DMEM_BLKSIZE2;
	} else {
		mem_size = falcon_falcon_hwcfg_imem_size_v(hwcfg_val)
			<< FALCON_DMEM_BLKSIZE2;
	}

	return mem_size;
}

u8 gk20a_falcon_get_ports_count(struct nvgpu_falcon *flcn,
		enum falcon_mem_type mem_type)
{
	u8 ports = 0U;
	u32 hwcfg1_val = 0U;

	hwcfg1_val = gk20a_falcon_readl(flcn, falcon_falcon_hwcfg1_r());

	if (mem_type == MEM_DMEM) {
		ports = (u8) falcon_falcon_hwcfg1_dmem_ports_v(hwcfg1_val);
	} else {
		ports = (u8) falcon_falcon_hwcfg1_imem_ports_v(hwcfg1_val);
	}

	return ports;
}

int gk20a_falcon_copy_to_dmem(struct nvgpu_falcon *flcn,
		u32 dst, u8 *src, u32 size, u8 port)
{
	u32 i = 0U, words = 0U, bytes = 0U;
	u32 data = 0U, addr_mask = 0U;
	u32 *src_u32 = (u32 *)src;

	nvgpu_log_fn(flcn->g, "dest dmem offset - %x, size - %x", dst, size);

	words = size >> 2U;
	bytes = size & 0x3U;

	addr_mask = falcon_falcon_dmemc_offs_m() |
		falcon_falcon_dmemc_blk_m();

	dst &= addr_mask;

	gk20a_falcon_writel(flcn, falcon_falcon_dmemc_r(port),
			    dst | falcon_falcon_dmemc_aincw_f(1));

	for (i = 0; i < words; i++) {
		gk20a_falcon_writel(flcn, falcon_falcon_dmemd_r(port),
				    src_u32[i]);
	}

	if (bytes > 0U) {
		data = 0;
		nvgpu_memcpy((u8 *)&data, &src[words << 2U], bytes);
		gk20a_falcon_writel(flcn, falcon_falcon_dmemd_r(port), data);
	}

	size = ALIGN(size, 4U);
	data = gk20a_falcon_readl(flcn, falcon_falcon_dmemc_r(port)) &
		addr_mask;
	if (data != (nvgpu_safe_add_u32(dst, size) & addr_mask)) {
		nvgpu_warn(flcn->g, "copy failed. bytes written %d, expected %d",
			data - dst, size);
		return -EIO;
	}

	return 0;
}

int gk20a_falcon_copy_to_imem(struct nvgpu_falcon *flcn, u32 dst,
		u8 *src, u32 size, u8 port, bool sec, u32 tag)
{
	u32 *src_u32 = (u32 *)src;
	u32 words = 0U;
	u32 blk = 0U;
	u32 i = 0U;

	nvgpu_log_info(flcn->g, "upload %d bytes to 0x%x", size, dst);

	words = size >> 2U;
	blk = dst >> 8;

	nvgpu_log_info(flcn->g, "upload %d words to 0x%x block %d, tag 0x%x",
			words, dst, blk, tag);

	gk20a_falcon_writel(flcn, falcon_falcon_imemc_r(port),
			falcon_falcon_imemc_offs_f(dst >> 2) |
			falcon_falcon_imemc_blk_f(blk) |
			/* Set Auto-Increment on write */
			falcon_falcon_imemc_aincw_f(1) |
			falcon_falcon_imemc_secure_f(sec ? 1U : 0U));

	for (i = 0U; i < words; i++) {
		if (i % 64U == 0U) {
			/* tag is always 256B aligned */
			gk20a_falcon_writel(flcn, falcon_falcon_imemt_r(port),
					    tag);
			tag = nvgpu_safe_add_u32(tag, 1U);
		}

		gk20a_falcon_writel(flcn, falcon_falcon_imemd_r(port),
				    src_u32[i]);
	}

	/* WARNING : setting remaining bytes in block to 0x0 */
	while (i % 64U != 0U) {
		gk20a_falcon_writel(flcn, falcon_falcon_imemd_r(port), 0);
		i++;
	}

	return 0;
}

int gk20a_falcon_bootstrap(struct nvgpu_falcon *flcn,
	u32 boot_vector)
{
	nvgpu_log_info(flcn->g, "boot vec 0x%x", boot_vector);

	gk20a_falcon_writel(flcn, falcon_falcon_dmactl_r(),
		falcon_falcon_dmactl_require_ctx_f(0));

	gk20a_falcon_writel(flcn, falcon_falcon_bootvec_r(),
		falcon_falcon_bootvec_vec_f(boot_vector));

	gk20a_falcon_writel(flcn, falcon_falcon_cpuctl_r(),
		falcon_falcon_cpuctl_startcpu_f(1));

	return 0;
}

u32 gk20a_falcon_mailbox_read(struct nvgpu_falcon *flcn,
		u32 mailbox_index)
{
	return gk20a_falcon_readl(flcn, mailbox_index != 0U ?
					falcon_falcon_mailbox1_r() :
					falcon_falcon_mailbox0_r());
}

void gk20a_falcon_mailbox_write(struct nvgpu_falcon *flcn,
		u32 mailbox_index, u32 data)
{
	gk20a_falcon_writel(flcn, mailbox_index != 0U ?
					falcon_falcon_mailbox1_r() :
					falcon_falcon_mailbox0_r(), data);
}

#ifdef CONFIG_NVGPU_FALCON_DEBUG
static void gk20a_falcon_dump_imblk(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = NULL;
	u32 i = 0U, j = 0U;
	u32 data[8] = {0U};
	u32 block_count = 0U;

	g = flcn->g;

	block_count = falcon_falcon_hwcfg_imem_size_v(
				gk20a_falcon_readl(flcn,
						   falcon_falcon_hwcfg_r()));

	/* block_count must be multiple of 8 */
	block_count &= ~0x7U;
	nvgpu_err(g, "FALCON IMEM BLK MAPPING (PA->VA) (%d TOTAL):",
		block_count);

	for (i = 0U; i < block_count; i += 8U) {
		for (j = 0U; j < 8U; j++) {
			gk20a_falcon_writel(flcn, falcon_falcon_imctl_debug_r(),
			falcon_falcon_imctl_debug_cmd_f(0x2) |
			falcon_falcon_imctl_debug_addr_blk_f(i + j));

			data[j] = gk20a_falcon_readl(flcn,
						     falcon_falcon_imstat_r());
		}

		nvgpu_err(g, " %#04x: %#010x %#010x %#010x %#010x",
				i, data[0], data[1], data[2], data[3]);
		nvgpu_err(g, " %#04x: %#010x %#010x %#010x %#010x",
				i + 4U, data[4], data[5], data[6], data[7]);
	}
}

static void gk20a_falcon_dump_pc_trace(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = NULL;
	u32 trace_pc_count = 0U;
	u32 pc = 0U;
	u32 i = 0U;

	g = flcn->g;

	if ((gk20a_falcon_readl(flcn, falcon_falcon_sctl_r()) & 0x02U) != 0U) {
		nvgpu_err(g, " falcon is in HS mode, PC TRACE dump not supported");
		return;
	}

	trace_pc_count = falcon_falcon_traceidx_maxidx_v(
				gk20a_falcon_readl(flcn,
						   falcon_falcon_traceidx_r()));
	nvgpu_err(g,
		"PC TRACE (TOTAL %d ENTRIES. entry 0 is the most recent branch):",
		trace_pc_count);

	for (i = 0; i < trace_pc_count; i++) {
		gk20a_falcon_writel(flcn, falcon_falcon_traceidx_r(),
				    falcon_falcon_traceidx_idx_f(i));

		pc = falcon_falcon_tracepc_pc_v(
			gk20a_falcon_readl(flcn, falcon_falcon_tracepc_r()));
		nvgpu_err(g, "FALCON_TRACEPC(%d)  :  %#010x", i, pc);
	}
}

void gk20a_falcon_dump_stats(struct nvgpu_falcon *flcn)
{
	struct gk20a *g = NULL;
	unsigned int i;

	g = flcn->g;

	nvgpu_err(g, "<<< FALCON id-%d DEBUG INFORMATION - START >>>",
			flcn->flcn_id);

	/* imblk dump */
	gk20a_falcon_dump_imblk(flcn);
	/* PC trace dump */
	gk20a_falcon_dump_pc_trace(flcn);

	nvgpu_err(g, "FALCON ICD REGISTERS DUMP");

	for (i = 0U; i < 4U; i++) {
		gk20a_falcon_writel(flcn,
			falcon_falcon_icd_cmd_r(),
			falcon_falcon_icd_cmd_opc_rreg_f() |
			falcon_falcon_icd_cmd_idx_f(FALCON_REG_PC));
		nvgpu_err(g, "FALCON_REG_PC : 0x%x",
			gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));

		gk20a_falcon_writel(flcn, falcon_falcon_icd_cmd_r(),
			falcon_falcon_icd_cmd_opc_rreg_f() |
			falcon_falcon_icd_cmd_idx_f(FALCON_REG_SP));
		nvgpu_err(g, "FALCON_REG_SP : 0x%x",
			gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));
	}

	gk20a_falcon_writel(flcn, falcon_falcon_icd_cmd_r(),
		falcon_falcon_icd_cmd_opc_rreg_f() |
		falcon_falcon_icd_cmd_idx_f(FALCON_REG_IMB));
	nvgpu_err(g, "FALCON_REG_IMB : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));

	gk20a_falcon_writel(flcn, falcon_falcon_icd_cmd_r(),
		falcon_falcon_icd_cmd_opc_rreg_f() |
		falcon_falcon_icd_cmd_idx_f(FALCON_REG_DMB));
	nvgpu_err(g, "FALCON_REG_DMB : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));

	gk20a_falcon_writel(flcn, falcon_falcon_icd_cmd_r(),
		falcon_falcon_icd_cmd_opc_rreg_f() |
		falcon_falcon_icd_cmd_idx_f(FALCON_REG_CSW));
	nvgpu_err(g, "FALCON_REG_CSW : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));

	gk20a_falcon_writel(flcn, falcon_falcon_icd_cmd_r(),
		falcon_falcon_icd_cmd_opc_rreg_f() |
		falcon_falcon_icd_cmd_idx_f(FALCON_REG_CTX));
	nvgpu_err(g, "FALCON_REG_CTX : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));

	gk20a_falcon_writel(flcn, falcon_falcon_icd_cmd_r(),
		falcon_falcon_icd_cmd_opc_rreg_f() |
		falcon_falcon_icd_cmd_idx_f(FALCON_REG_EXCI));
	nvgpu_err(g, "FALCON_REG_EXCI : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));

	for (i = 0U; i < 6U; i++) {
		gk20a_falcon_writel(flcn, falcon_falcon_icd_cmd_r(),
			falcon_falcon_icd_cmd_opc_rreg_f() |
			falcon_falcon_icd_cmd_idx_f(
			falcon_falcon_icd_cmd_opc_rstat_f()));
		nvgpu_err(g, "FALCON_REG_RSTAT[%d] : 0x%x", i,
			gk20a_falcon_readl(flcn, falcon_falcon_icd_rdata_r()));
	}

	nvgpu_err(g, " FALCON REGISTERS DUMP");
	nvgpu_err(g, "falcon_falcon_os_r : %d",
		gk20a_falcon_readl(flcn, falcon_falcon_os_r()));
	nvgpu_err(g, "falcon_falcon_cpuctl_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_cpuctl_r()));
	nvgpu_err(g, "falcon_falcon_idlestate_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_idlestate_r()));
	nvgpu_err(g, "falcon_falcon_mailbox0_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_mailbox0_r()));
	nvgpu_err(g, "falcon_falcon_mailbox1_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_mailbox1_r()));
	nvgpu_err(g, "falcon_falcon_irqstat_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_irqstat_r()));
	nvgpu_err(g, "falcon_falcon_irqmode_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_irqmode_r()));
	nvgpu_err(g, "falcon_falcon_irqmask_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_irqmask_r()));
	nvgpu_err(g, "falcon_falcon_irqdest_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_irqdest_r()));
	nvgpu_err(g, "falcon_falcon_debug1_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_debug1_r()));
	nvgpu_err(g, "falcon_falcon_debuginfo_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_debuginfo_r()));
	nvgpu_err(g, "falcon_falcon_bootvec_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_bootvec_r()));
	nvgpu_err(g, "falcon_falcon_hwcfg_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_hwcfg_r()));
	nvgpu_err(g, "falcon_falcon_engctl_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_engctl_r()));
	nvgpu_err(g, "falcon_falcon_curctx_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_curctx_r()));
	nvgpu_err(g, "falcon_falcon_nxtctx_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_nxtctx_r()));
	nvgpu_err(g, "falcon_falcon_exterrstat_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_exterrstat_r()));
	nvgpu_err(g, "falcon_falcon_exterraddr_r : 0x%x",
		gk20a_falcon_readl(flcn, falcon_falcon_exterraddr_r()));
}
#endif
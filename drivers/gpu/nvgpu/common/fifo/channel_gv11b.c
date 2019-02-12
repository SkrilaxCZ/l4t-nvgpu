/*
 * Copyright (c) 2016-2019, NVIDIA CORPORATION.  All rights reserved.
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


#include <nvgpu/channel.h>
#include <nvgpu/log.h>
#include <nvgpu/atomic.h>
#include <nvgpu/io.h>
#include <nvgpu/gk20a.h>

#include "channel_gv11b.h"

#include <nvgpu/hw/gv11b/hw_ccsr_gv11b.h>

void gv11b_fifo_channel_unbind(struct channel_gk20a *ch)
{
	struct gk20a *g = ch->g;

	nvgpu_log_fn(g, " ");

	if (nvgpu_atomic_cmpxchg(&ch->bound, true, false) != 0) {
		gk20a_writel(g, ccsr_channel_inst_r(ch->chid),
			ccsr_channel_inst_ptr_f(0) |
			ccsr_channel_inst_bind_false_f());

		gk20a_writel(g, ccsr_channel_r(ch->chid),
			ccsr_channel_enable_clr_true_f() |
			ccsr_channel_pbdma_faulted_reset_f() |
			ccsr_channel_eng_faulted_reset_f());
	}
}

u32 gv11b_fifo_channel_count(struct gk20a *g)
{
	return ccsr_channel__size_1_v();
}

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 */

#include "i915_drv.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_vrr.h"

#ifndef VRR_FEATURE_NOT_SUPPORTED
static bool
intel_vrr_dp_is_capable(struct intel_connector *connector)
{
	const struct drm_display_info *info = &connector->base.display_info;
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_dp *intel_dp = intel_attached_dp(connector);

	/*
	 * DP Sink is capable of VRR video timings if
	 * Ignore MSA bit is set in DPCD.
	 * EDID monitor range also should be atleast 10 for reasonable
	 * Adaptive Sync or Variable Refresh Rate end user experience.
	 */
	if (!drm_dp_sink_can_do_video_without_timing_msa(intel_dp->dpcd))
		return false;

	return HAS_DP_VRR(i915) &&
	       info->monitor_range.max_vfreq - info->monitor_range.min_vfreq > 10;
}

static bool
intel_vrr_hdmi_is_capable(struct intel_connector *connector)
{
	struct drm_display_info *info = &connector->base.display_info;
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct drm_hdmi_info *hdmi = &info->hdmi;

	return HAS_HDMI_VRR(i915) && hdmi->vrr_cap.vrr_min > 0;
}

bool intel_vrr_is_capable(struct intel_connector *connector)
{
	switch (connector->base.connector_type) {
	case DRM_MODE_CONNECTOR_eDP:
		if (!connector->panel.vbt.vrr)
			return false;
		fallthrough;
	case DRM_MODE_CONNECTOR_DisplayPort:
		return intel_vrr_dp_is_capable(connector);
	case DRM_MODE_CONNECTOR_HDMIA:
		fallthrough;
	case DRM_MODE_CONNECTOR_HDMIB:
		return intel_vrr_hdmi_is_capable(connector);
	default:
		return false;
	}
}
#else
bool intel_vrr_is_capable(struct intel_connector *connector)
{
       const struct drm_display_info *info = &connector->base.display_info;
       struct drm_i915_private *i915 = to_i915(connector->base.dev);
       struct intel_dp *intel_dp;

        /*
         * DP Sink is capable of VRR video timings if
         * Ignore MSA bit is set in DPCD.
         * EDID monitor range also should be atleast 10 for reasonable
         * Adaptive Sync or Variable Refresh Rate end user experience.
         */
        switch (connector->base.connector_type) {
        case DRM_MODE_CONNECTOR_eDP:
                if (!connector->panel.vbt.vrr)
                        return false;
                fallthrough;
        case DRM_MODE_CONNECTOR_DisplayPort:
               intel_dp = intel_attached_dp(connector);

               if (!drm_dp_sink_can_do_video_without_timing_msa(intel_dp->dpcd))
                       return false;

               break;
        default:
                return false;
        }

       return HAS_VRR(i915) &&
               info->monitor_range.max_vfreq - info->monitor_range.min_vfreq > 10;
}
#endif

void
intel_vrr_check_modeset(struct intel_atomic_state *state)
{
	int i;
	struct intel_crtc_state *old_crtc_state, *new_crtc_state;
	struct intel_crtc *crtc;

	for_each_oldnew_intel_crtc_in_state(state, crtc, old_crtc_state,
					    new_crtc_state, i) {
		if (new_crtc_state->uapi.vrr_enabled !=
		    old_crtc_state->uapi.vrr_enabled)
			new_crtc_state->uapi.mode_changed = true;
	}
}

/*
 * Without VRR registers get latched at:
 *  vblank_start
 *
 * With VRR the earliest registers can get latched is:
 *  intel_vrr_vmin_vblank_start(), which if we want to maintain
 *  the correct min vtotal is >=vblank_start+1
 *
 * The latest point registers can get latched is the vmax decision boundary:
 *  intel_vrr_vmax_vblank_start()
 *
 * Between those two points the vblank exit starts (and hence registers get
 * latched) ASAP after a push is sent.
 *
 * framestart_delay is programmable 1-4.
 */
static int intel_vrr_vblank_exit_length(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	/* The hw imposes the extra scanline before frame start */
	if (DISPLAY_VER(i915) >= 13)
		return crtc_state->vrr.guardband + crtc_state->framestart_delay + 1;
	else
		return crtc_state->vrr.pipeline_full + crtc_state->framestart_delay + 1;
}

int intel_vrr_vmin_vblank_start(const struct intel_crtc_state *crtc_state)
{
	/* Min vblank actually determined by flipline that is always >=vmin+1 */
	return crtc_state->vrr.vmin + 1 - intel_vrr_vblank_exit_length(crtc_state);
}

int intel_vrr_vmax_vblank_start(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->vrr.vmax - intel_vrr_vblank_exit_length(crtc_state);
}

#ifndef VRR_FEATURE_NOT_SUPPORTED
static void
intel_vrr_hdmi_get_vmin_max(struct drm_display_mode *adjusted_mode,
			    struct intel_connector *connector,
			    int *vmin, int *vmax)
{
	struct drm_display_info *info = &connector->base.display_info;
	struct drm_hdmi_info *hdmi = &info->hdmi;
	int brr, vrr_max;

	brr = drm_mode_vrefresh(adjusted_mode);
	vrr_max = hdmi->vrr_cap.vrr_max;
	/*
	 * As per HDMI2.1 spec, VRR max is optional and lowest valid value is
	 * 100 Hz. For cases where VRR max is not given or when BRR
	 * (Base Refresh-Rate) is less than or equal to VRR max, the
	 * upper-limit is taken as the BRR only.
	 * Vmin in that case will be equal to VTotal.
	 */
	if (vrr_max < 100 || brr <= vrr_max)
		*vmin = adjusted_mode->crtc_vtotal;
	else
		*vmin = DIV_ROUND_UP(adjusted_mode->crtc_clock * 1000,
				     adjusted_mode->crtc_htotal * vrr_max);

	*vmax = adjusted_mode->crtc_clock * 1000 /
		(adjusted_mode->crtc_htotal * hdmi->vrr_cap.vrr_min);
}

static void
intel_vrr_dp_get_vmin_max(struct drm_display_mode *adjusted_mode,
			  struct intel_connector *connector,
			  int *vmin, int *vmax)
{
	const struct drm_display_info *info = &connector->base.display_info;

	*vmin = DIV_ROUND_UP(adjusted_mode->crtc_clock * 1000,
			     adjusted_mode->crtc_htotal * info->monitor_range.max_vfreq);
	*vmax = adjusted_mode->crtc_clock * 1000 /
		(adjusted_mode->crtc_htotal * info->monitor_range.min_vfreq);
}
#endif

void
intel_vrr_compute_config(struct intel_crtc_state *crtc_state,
			 struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
#ifdef VRR_FEATURE_NOT_SUPPORTED
	const struct drm_display_info *info = &connector->base.display_info;
#endif
	int vmin, vmax;

	if (!intel_vrr_is_capable(connector))
		return;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		return;

	if (!crtc_state->uapi.vrr_enabled)
		return;

#ifndef VRR_FEATURE_NOT_SUPPORTED
	if (connector->base.connector_type == DRM_MODE_CONNECTOR_eDP ||
	    connector->base.connector_type == DRM_MODE_CONNECTOR_DisplayPort)
		intel_vrr_dp_get_vmin_max(adjusted_mode, connector, &vmin, &vmax);

	else if (connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIA ||
		 connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIB)
		intel_vrr_hdmi_get_vmin_max(adjusted_mode, connector, &vmin, &vmax);

	else
		return;
#else
       vmin = DIV_ROUND_UP(adjusted_mode->crtc_clock * 1000,
                           adjusted_mode->crtc_htotal * info->monitor_range.max_vfreq);
       vmax = adjusted_mode->crtc_clock * 1000 /
               (adjusted_mode->crtc_htotal * info->monitor_range.min_vfreq);
#endif

	vmin = max_t(int, vmin, adjusted_mode->crtc_vtotal);
	vmax = max_t(int, vmax, adjusted_mode->crtc_vtotal);

	if (vmin >= vmax)
		return;

	/*
	 * flipline determines the min vblank length the hardware will
	 * generate, and flipline>=vmin+1, hence we reduce vmin by one
	 * to make sure we can get the actual min vblank length.
	 */
	crtc_state->vrr.vmin = vmin - 1;
	crtc_state->vrr.vmax = vmax;

#ifndef VRR_FEATURE_NOT_SUPPORTED
	if (connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIB) {
		crtc_state->vrr.vsync_start = adjusted_mode->crtc_vsync_start;
		crtc_state->vrr.vsync_end = adjusted_mode->crtc_vsync_end;
	}
#endif
	crtc_state->vrr.enable = true;

	crtc_state->vrr.flipline = crtc_state->vrr.vmin + 1;

	/*
	 * For XE_LPD+, we use guardband and pipeline override
	 * is deprecated.
	 */
	if (DISPLAY_VER(i915) >= 13) {
		/*
		 * FIXME: Subtract Window2 delay from below value.
		 *
		 * Window2 specifies time required to program DSB (Window2) in
		 * number of scan lines. Assuming 0 for no DSB.
		 */
		crtc_state->vrr.guardband =
			crtc_state->vrr.vmin - adjusted_mode->crtc_vdisplay;
	} else {
		/*
		 * FIXME: s/4/framestart_delay/ to get consistent
		 * earliest/latest points for register latching regardless
		 * of the framestart_delay used?
		 *
		 * FIXME: this really needs the extra scanline to provide consistent
		 * behaviour for all framestart_delay values. Otherwise with
		 * framestart_delay==4 we will end up extending the min vblank by
		 * one extra line.
		 */
		crtc_state->vrr.pipeline_full =
			min(255, crtc_state->vrr.vmin - adjusted_mode->crtc_vdisplay - 4 - 1);
	}

	crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
}

#ifndef VRR_FEATURE_NOT_SUPPORTED
void intel_vrr_enable(struct intel_connector *connector,
		      const struct intel_crtc_state *crtc_state)
#else
void intel_vrr_enable(struct intel_encoder *encoder,
		      const struct intel_crtc_state *crtc_state)
#endif
{
#ifndef VRR_FEATURE_NOT_SUPPORTED
	struct intel_encoder *encoder = intel_attached_encoder(connector);
#endif
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
#ifndef VRR_FEATURE_NOT_SUPPORTED
	u32 trans_vrr_ctl, trans_vrr_vsync;
#else
	u32 trans_vrr_ctl;
#endif

	if (!crtc_state->vrr.enable)
		return;

	if (DISPLAY_VER(dev_priv) >= 13)
		trans_vrr_ctl = VRR_CTL_VRR_ENABLE |
			VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			XELPD_VRR_CTL_VRR_GUARDBAND(crtc_state->vrr.guardband);
	else
		trans_vrr_ctl = VRR_CTL_VRR_ENABLE |
			VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			VRR_CTL_PIPELINE_FULL(crtc_state->vrr.pipeline_full) |
			VRR_CTL_PIPELINE_FULL_OVERRIDE;

	intel_de_write(dev_priv, TRANS_VRR_VMIN(cpu_transcoder), crtc_state->vrr.vmin - 1);
	intel_de_write(dev_priv, TRANS_VRR_VMAX(cpu_transcoder), crtc_state->vrr.vmax - 1);

#ifndef VRR_FEATURE_NOT_SUPPORTED
	if (connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIB) {
		trans_vrr_vsync = VRR_VSYNC_START(crtc_state->vrr.vsync_start) |
				  VRR_VSYNC_END(crtc_state->vrr.vsync_end);
		intel_de_write(dev_priv, TRANS_VRR_VSYNC(cpu_transcoder), trans_vrr_vsync);
	}
#endif

	intel_de_write(dev_priv, TRANS_VRR_CTL(cpu_transcoder), trans_vrr_ctl);
	intel_de_write(dev_priv, TRANS_VRR_FLIPLINE(cpu_transcoder), crtc_state->vrr.flipline - 1);
	intel_de_write(dev_priv, TRANS_PUSH(cpu_transcoder), TRANS_PUSH_EN);
}

void intel_vrr_send_push(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return;

	intel_de_write(dev_priv, TRANS_PUSH(cpu_transcoder),
		       TRANS_PUSH_EN | TRANS_PUSH_SEND);
}

bool intel_vrr_is_push_sent(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (!crtc_state->vrr.enable)
		return false;

	return intel_de_read(dev_priv, TRANS_PUSH(cpu_transcoder)) & TRANS_PUSH_SEND;
}

void intel_vrr_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

	if (!old_crtc_state->vrr.enable)
		return;

	intel_de_write(dev_priv, TRANS_VRR_CTL(cpu_transcoder), 0);
	intel_de_write(dev_priv, TRANS_PUSH(cpu_transcoder), 0);
}

void intel_vrr_get_config(struct intel_crtc *crtc,
			  struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
#ifndef VRR_FEATURE_NOT_SUPPORTED
	u32 trans_vrr_ctl, trans_vrr_vsync;
#else
	u32 trans_vrr_ctl;
#endif

	trans_vrr_ctl = intel_de_read(dev_priv, TRANS_VRR_CTL(cpu_transcoder));
	crtc_state->vrr.enable = trans_vrr_ctl & VRR_CTL_VRR_ENABLE;
	if (!crtc_state->vrr.enable)
		return;

	if (DISPLAY_VER(dev_priv) >= 13)
		crtc_state->vrr.guardband =
			REG_FIELD_GET(XELPD_VRR_CTL_VRR_GUARDBAND_MASK, trans_vrr_ctl);
	else
		if (trans_vrr_ctl & VRR_CTL_PIPELINE_FULL_OVERRIDE)
			crtc_state->vrr.pipeline_full =
				REG_FIELD_GET(VRR_CTL_PIPELINE_FULL_MASK, trans_vrr_ctl);
	if (trans_vrr_ctl & VRR_CTL_FLIP_LINE_EN)
		crtc_state->vrr.flipline = intel_de_read(dev_priv, TRANS_VRR_FLIPLINE(cpu_transcoder)) + 1;
	crtc_state->vrr.vmax = intel_de_read(dev_priv, TRANS_VRR_VMAX(cpu_transcoder)) + 1;
	crtc_state->vrr.vmin = intel_de_read(dev_priv, TRANS_VRR_VMIN(cpu_transcoder)) + 1;

#ifndef VRR_FEATURE_NOT_SUPPORTED
	trans_vrr_vsync = intel_de_read(dev_priv, TRANS_VRR_VSYNC(cpu_transcoder));
	crtc_state->vrr.vsync_start = REG_FIELD_GET(VRR_VSYNC_START_MASK, trans_vrr_vsync);
	crtc_state->vrr.vsync_end = REG_FIELD_GET(VRR_VSYNC_END_MASK, trans_vrr_vsync);

#endif
	crtc_state->mode_flags |= I915_MODE_FLAG_VRR;
}

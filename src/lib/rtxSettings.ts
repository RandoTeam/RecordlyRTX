import { loadAppSetting, saveAppSetting } from './appSettings';
import type { RtxPreset, RtxRecordingSettings } from '../types/rtxSettings';

export const RTX_SETTINGS_KEY = 'rtxRecordingSettings';

export const DEFAULT_RTX_SETTINGS: RtxRecordingSettings = Object.freeze({
	preset: 'balanced',
	codec: 'hevc',
	fps: 60,
	rateControl: 'vbr',
	bitrateMbps: 35,
	cqpLevel: 20,
	zeroCopy: true,
});

export function applyRtxPreset(preset: RtxPreset): RtxRecordingSettings {
	switch (preset) {
		case 'ultra-performance':
			return {
				preset: 'ultra-performance',
				codec: 'h264',
				fps: 60,
				rateControl: 'vbr',
				bitrateMbps: 20,
				cqpLevel: 26,
				zeroCopy: true,
			};
		case 'max-quality':
			return {
				preset: 'max-quality',
				codec: 'hevc',
				fps: 60,
				rateControl: 'cqp',
				bitrateMbps: 60,
				cqpLevel: 18,
				zeroCopy: true,
			};
		case 'balanced':
		default:
			return {
				...DEFAULT_RTX_SETTINGS,
				preset: 'balanced',
			};
	}
}

export function loadRtxSettings(): RtxRecordingSettings {
	const stored = loadAppSetting<Partial<RtxRecordingSettings>>(RTX_SETTINGS_KEY);
	if (!stored || typeof stored !== 'object') {
		return { ...DEFAULT_RTX_SETTINGS };
	}
	return {
		...DEFAULT_RTX_SETTINGS,
		...stored,
	};
}

export function saveRtxSettings(settings: RtxRecordingSettings): boolean {
	return saveAppSetting(RTX_SETTINGS_KEY, settings);
}

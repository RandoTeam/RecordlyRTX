import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
	DEFAULT_RTX_SETTINGS,
	loadRtxSettings,
	saveRtxSettings,
	applyRtxPreset,
	RTX_SETTINGS_KEY,
} from './rtxSettings';
import * as appSettings from './appSettings';

vi.mock('./appSettings', () => ({
	loadAppSetting: vi.fn(),
	saveAppSetting: vi.fn(),
}));

describe('rtxSettings', () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});

	it('returns default settings when nothing is stored', () => {
		vi.mocked(appSettings.loadAppSetting).mockReturnValue(null);
		const settings = loadRtxSettings();
		expect(settings).toEqual(DEFAULT_RTX_SETTINGS);
		expect(settings.preset).toBe('balanced');
		expect(settings.fps).toBe(60);
		expect(settings.codec).toBe('hevc');
		expect(settings.rateControl).toBe('vbr');
		expect(settings.bitrateMbps).toBe(35);
		expect(settings.zeroCopy).toBe(true);
	});

	it('correctly calculates preset configurations', () => {
		const perf = applyRtxPreset('ultra-performance');
		expect(perf.preset).toBe('ultra-performance');
		expect(perf.fps).toBe(60);
		expect(perf.codec).toBe('h264');
		expect(perf.rateControl).toBe('vbr');
		expect(perf.bitrateMbps).toBe(20);

		const maxQ = applyRtxPreset('max-quality');
		expect(maxQ.preset).toBe('max-quality');
		expect(maxQ.fps).toBe(60);
		expect(maxQ.codec).toBe('hevc');
		expect(maxQ.rateControl).toBe('cqp');
		expect(maxQ.cqpLevel).toBe(18);

		const balanced = applyRtxPreset('balanced');
		expect(balanced.preset).toBe('balanced');
		expect(balanced.codec).toBe('hevc');
		expect(balanced.bitrateMbps).toBe(35);
	});

	it('loads stored settings merged with defaults', () => {
		vi.mocked(appSettings.loadAppSetting).mockReturnValue({
			preset: 'custom',
			fps: 120,
			bitrateMbps: 80,
		});
		const settings = loadRtxSettings();
		expect(settings.preset).toBe('custom');
		expect(settings.fps).toBe(120);
		expect(settings.bitrateMbps).toBe(80);
		expect(settings.codec).toBe('hevc'); // default preserved
	});

	it('saves settings through appSettings', () => {
		saveRtxSettings({ ...DEFAULT_RTX_SETTINGS, fps: 120 });
		expect(appSettings.saveAppSetting).toHaveBeenCalledWith(
			RTX_SETTINGS_KEY,
			expect.objectContaining({ fps: 120 }),
		);
	});
});

export type RtxPreset = 'ultra-performance' | 'balanced' | 'max-quality' | 'custom';
export type RtxCodec = 'h264' | 'hevc';
export type RtxFramerate = 30 | 60 | 120;
export type RtxRateControl = 'cqp' | 'vbr' | 'cbr';

export interface RtxRecordingSettings {
	preset: RtxPreset;
	codec: RtxCodec;
	fps: RtxFramerate;
	rateControl: RtxRateControl;
	bitrateMbps: number;
	cqpLevel: number;
	zeroCopy: boolean;
}

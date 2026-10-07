import { memo, useCallback, useEffect, useState } from "react";
import { SettingsRow } from "../SettingsRow";
import {
	Select,
	SelectContent,
	SelectItem,
	SelectTrigger,
	SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import { SliderControl } from "../SliderControl";
import {
	DEFAULT_RTX_SETTINGS,
	applyRtxPreset,
	loadRtxSettings,
	saveRtxSettings,
} from "@/lib/rtxSettings";
import type {
	RtxCodec,
	RtxFramerate,
	RtxPreset,
	RtxRateControl,
	RtxRecordingSettings,
} from "@/types/rtxSettings";

export const RtxRecordingSettingsSection = memo(function RtxRecordingSettingsSection() {
	const [settings, setSettings] = useState<RtxRecordingSettings>(DEFAULT_RTX_SETTINGS);
	const [initialized, setInitialized] = useState(false);

	useEffect(() => {
		const loaded = loadRtxSettings();
		setSettings(loaded);
		setInitialized(true);
	}, []);

	const updateSettings = useCallback((patch: Partial<RtxRecordingSettings>) => {
		setSettings((current) => {
			const updated: RtxRecordingSettings = {
				...current,
				...patch,
			};
			saveRtxSettings(updated);
			return updated;
		});
	}, []);

	const handlePresetChange = useCallback((preset: RtxPreset) => {
		if (preset === "custom") {
			updateSettings({ preset: "custom" });
		} else {
			const presetConfig = applyRtxPreset(preset);
			setSettings(presetConfig);
			saveRtxSettings(presetConfig);
		}
	}, [updateSettings]);

	const handleCodecChange = useCallback((codec: RtxCodec) => {
		updateSettings({ codec, preset: "custom" });
	}, [updateSettings]);

	const handleFpsChange = useCallback((fpsStr: string) => {
		const fps = Number(fpsStr) as RtxFramerate;
		updateSettings({ fps, preset: "custom" });
	}, [updateSettings]);

	const handleRateControlChange = useCallback((rateControl: RtxRateControl) => {
		updateSettings({ rateControl, preset: "custom" });
	}, [updateSettings]);

	const handleBitrateChange = useCallback((bitrateMbps: number) => {
		updateSettings({ bitrateMbps, preset: "custom" });
	}, [updateSettings]);

	const handleCqpChange = useCallback((cqpLevel: number) => {
		updateSettings({ cqpLevel, preset: "custom" });
	}, [updateSettings]);

	const handleZeroCopyChange = useCallback((zeroCopy: boolean) => {
		updateSettings({ zeroCopy });
	}, [updateSettings]);

	if (!initialized) return null;

	return (
		<div className="flex flex-col gap-4 border-t border-border/40 pt-4">
			<div className="flex flex-col gap-1">
				<h2 className="text-sm font-semibold tracking-tight text-foreground">
					NVIDIA RTX Recording Engine
				</h2>
				<p className="text-xs text-muted-foreground">
					Zero-copy hardware encoding using dedicated NVENC ASIC on NVIDIA RTX GPUs.
				</p>
			</div>

			<SettingsRow
				title="Quality Preset"
				description="Select an optimized RTX hardware profile or customize individual settings."
			>
				<Select
					value={settings.preset}
					onValueChange={(val) => handlePresetChange(val as RtxPreset)}
				>
					<SelectTrigger className="w-52">
						<SelectValue placeholder="Select preset" />
					</SelectTrigger>
					<SelectContent>
						<SelectItem value="balanced">Balanced (HEVC 60fps)</SelectItem>
						<SelectItem value="ultra-performance">Ultra Performance (Low Overhead)</SelectItem>
						<SelectItem value="max-quality">Max Quality (Studio CQP)</SelectItem>
						<SelectItem value="custom">Custom Configuration</SelectItem>
					</SelectContent>
				</Select>
			</SettingsRow>

			<SettingsRow
				title="Video Codec"
				description="HEVC provides up to 50% better compression efficiency than H.264 at identical quality."
			>
				<Select
					value={settings.codec}
					onValueChange={(val) => handleCodecChange(val as RtxCodec)}
				>
					<SelectTrigger className="w-52">
						<SelectValue placeholder="Select codec" />
					</SelectTrigger>
					<SelectContent>
						<SelectItem value="hevc">HEVC / H.265 (Recommended)</SelectItem>
						<SelectItem value="h264">H.264 / AVC</SelectItem>
					</SelectContent>
				</Select>
			</SettingsRow>

			<SettingsRow
				title="Target Framerate"
				description="Higher frame rates deliver smoother motion on high-refresh displays."
			>
				<Select
					value={String(settings.fps)}
					onValueChange={handleFpsChange}
				>
					<SelectTrigger className="w-52">
						<SelectValue placeholder="Select framerate" />
					</SelectTrigger>
					<SelectContent>
						<SelectItem value="30">30 FPS (Compact files)</SelectItem>
						<SelectItem value="60">60 FPS (Smooth default)</SelectItem>
						<SelectItem value="120">120 FPS (High-motion)</SelectItem>
					</SelectContent>
				</Select>
			</SettingsRow>

			<SettingsRow
				title="Rate Control Mode"
				description="CQP guarantees visual fidelity regardless of motion complexity."
			>
				<Select
					value={settings.rateControl}
					onValueChange={(val) => handleRateControlChange(val as RtxRateControl)}
				>
					<SelectTrigger className="w-52">
						<SelectValue placeholder="Select mode" />
					</SelectTrigger>
					<SelectContent>
						<SelectItem value="vbr">Variable Bitrate (VBR)</SelectItem>
						<SelectItem value="cqp">Constant Quality (CQP)</SelectItem>
						<SelectItem value="cbr">Constant Bitrate (CBR)</SelectItem>
					</SelectContent>
				</Select>
			</SettingsRow>

			{settings.rateControl !== "cqp" ? (
				<SettingsRow
					title="Video Bitrate"
					description="Target transmission bandwidth for the video stream."
					stacked
				>
					<SliderControl
						label="Bitrate"
						value={settings.bitrateMbps}
						min={5}
						max={120}
						step={5}
						onChange={handleBitrateChange}
						formatValue={(v) => `${v} Mbps`}
					/>
				</SettingsRow>
			) : (
				<SettingsRow
					title="CQP Quantization Level"
					description="Quantization factor (lower values produce sharper image, higher bitrates)."
					stacked
				>
					<SliderControl
						label="QP Factor"
						value={settings.cqpLevel}
						min={16}
						max={32}
						step={1}
						onChange={handleCqpChange}
						formatValue={(v) => `QP ${v} (${v <= 18 ? "Studio" : v <= 23 ? "High" : "Standard"})`}
					/>
				</SettingsRow>
			)}

			<SettingsRow
				title="Zero-Copy VRAM Pipeline"
				description="Streams Direct3D 11 desktop surfaces straight to NVENC without copying frames into CPU RAM."
			>
				<Switch
					aria-label="Toggle Zero-Copy VRAM Pipeline"
					checked={settings.zeroCopy}
					onCheckedChange={handleZeroCopyChange}
				/>
			</SettingsRow>
		</div>
	);
});

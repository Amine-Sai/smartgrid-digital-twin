import React, { useMemo } from 'react';
import { Box, Typography } from '@mui/material';
import simData from '../data/simulation.json';
import {
    XAxis, YAxis, CartesianGrid,
    ResponsiveContainer, Tooltip as RechartsTooltip, Area, AreaChart,
} from 'recharts';

const SCALE = 2;
export const FAULT_EVENTS = simData.meta?.fault_events || [];
export const ALL_RECORDS = (simData.series?.timestamps || []).map((ts, i) => {
    const indActual = Number(simData.series.industrial_W?.[i] ?? 0) * SCALE;
    const resActual = Math.min(Number(simData.series.residential_W?.[i] ?? 0) * SCALE, indActual * 0.5);
    const resStreet = Number(simData.series.res_street_W?.[i] ?? 0) * SCALE * 10;
    const indStreet = Number(simData.series.ind_street_W?.[i] ?? 0) * SCALE * 10;
    const factory = indActual * 1.2;
    const substation = factory * 0.5;
    const totalGrid = resActual + indActual + resStreet + indStreet + factory + substation;
    const temp = (simData.series.temperature_C?.[i] ?? 0) + 20;
    const hum = simData.series.humidity_pct?.[i] ?? 0;

    return {
        timestamp: ts,
        date: new Date(ts),
        hour: new Date(ts).getHours(),
        residential: resActual,
        industrial: indActual,
        factory,
        substation,
        totalGrid,
        resStreet,
        indStreet,
        temperature: temp,
        humidity: hum,
    };
});
export const TOTAL_HOURS = ALL_RECORDS.length;

const ALL_LABELS = ALL_RECORDS.map(d =>
    new Date(d.timestamp).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' })
);
const ALL_TIMESTAMPS_LONG = ALL_RECORDS.map(d =>
    new Date(d.timestamp).toLocaleString('en-US', {
        weekday: 'short', month: 'short', day: 'numeric',
        hour: '2-digit', minute: '2-digit'
    })
);

const FAULT_RANGES = (FAULT_EVENTS || []).filter(Boolean).map(ev => {
    const start = ev.start ? new Date(ev.start).getTime()
        : (ev.timestamp ? new Date(ev.timestamp).getTime() : NaN);
    const end = ev.end ? new Date(ev.end).getTime()
        : (Number.isFinite(start) ? start + 6 * 3600 * 1000 : NaN);
    return { zone: ev.zone, start, end };
}).filter(r => Number.isFinite(r.start) && Number.isFinite(r.end));

const ALL_EPOCHS = ALL_RECORDS.map(d => new Date(d.timestamp).getTime());

const STATUS_FILL = {
    ok: '#22c55e',
    fault: '#ef4444',
    disabled: '#6b7280',
};
const STATUS_STROKE = {
    ok: '#15803d',
    fault: '#b91c1c',
    disabled: '#4b5563',
};
const STATUS_LABEL = {
    ok: 'Normal',
    fault: 'Anomaly',
    disabled: 'Inactive',
};

const ZONES = [
    { id: 'Industrial', label: 'INDUSTRIAL ZONE', dataKey: 'industrial', shape: 'rect', x: 60, y: 35, w: 320, h: 130 },
    { id: 'Residential', label: 'RESIDENTIAL ZONE', dataKey: 'residential', shape: 'rect', x: 530, y: 35, w: 340, h: 120 },
    { id: 'Stadium', label: 'STADIUM', dataKey: null, shape: 'ellipse', cx: 170, cy: 430, rx: 155, ry: 75, disabled: true },
    { id: 'Factory', label: 'FACTORY', dataKey: 'factory', shape: 'rect', x: 500, y: 370, w: 200, h: 120 },
    { id: 'Substation', label: 'SUBSTATION', dataKey: 'substation', shape: 'rect', x: 740, y: 370, w: 140, h: 140 },
];

const STREETLIGHTS = [
    { x: 80, y: 200, zone: 'Industrial' },
    { x: 175, y: 200, zone: 'Industrial' },
    { x: 270, y: 200, zone: 'Industrial' },
    { x: 360, y: 200, zone: 'Industrial' },
    { x: 560, y: 195, zone: 'Residential' },
    { x: 660, y: 195, zone: 'Residential' },
    { x: 760, y: 195, zone: 'Residential' },
    { x: 85, y: 320, zone: 'Stadium' },
    { x: 180, y: 320, zone: 'Stadium' },
    { x: 275, y: 335, zone: 'Stadium' },
    { x: 530, y: 330, zone: 'Factory' },
    { x: 630, y: 320, zone: 'Factory' },
    { x: 770, y: 310, zone: 'Substation' },
    { x: 860, y: 310, zone: 'Substation' },
];

function getStreetlightStatus(power) {
    if (power >= 0.7) return 'ok';
    return 'fault';
}

const textShadowStyle = { textShadow: '0 1px 4px rgba(0,0,0,0.5)' };
const chartTooltipStyle = {
    background: '#111f35', border: '1px solid rgba(255,255,255,0.1)',
    borderRadius: 10, fontSize: 11,
};

function StreetLight({ x, y, status = 'ok' }) {
    const color = STATUS_FILL[status];
    const strokeColor = STATUS_STROKE[status];
    return (
        <g>
            <circle cx={x} cy={y} r={8} fill="none" stroke={strokeColor} strokeWidth={2} />
            {status === 'fault' && (
                <circle cx={x} cy={y} r={12} fill={color} opacity={0.15}>
                    <animate attributeName="r" values="12;18;12" dur="1.5s" repeatCount="indefinite" />
                    <animate attributeName="opacity" values="0.15;0.05;0.15" dur="1.5s" repeatCount="indefinite" />
                </circle>
            )}
        </g>
    );
}

/* ─── Building Zone component ────────────────────────────────────────────── */
function BuildingZone({ zone, status, liveValue }) {
    const isDisabled = zone.disabled;
    const effectiveStatus = isDisabled ? 'disabled' : status;
    const fill = STATUS_FILL[effectiveStatus];
    const stroke = STATUS_STROKE[effectiveStatus];
    const isFault = effectiveStatus === 'fault';
    const displayValue = isDisabled ? 0 : liveValue;

    if (zone.shape === 'ellipse') {
        return (
            <g opacity={isDisabled ? 0.5 : 1}>
                {isFault && (
                    <ellipse cx={zone.cx} cy={zone.cy} rx={zone.rx + 6} ry={zone.ry + 6}
                        fill={fill} opacity={0.12}>
                        <animate attributeName="opacity" values="0.12;0.25;0.12" dur="1.4s" repeatCount="indefinite" />
                    </ellipse>
                )}
                <ellipse cx={zone.cx} cy={zone.cy} rx={zone.rx} ry={zone.ry}
                    fill={fill} stroke={stroke} strokeWidth={3} />
                <text x={zone.cx} y={zone.cy - 8} textAnchor="middle"
                    fontSize={13} fontWeight="800" fill="#fff" fontFamily="Inter,sans-serif"
                    style={textShadowStyle}>
                    {zone.label}
                </text>
                <text x={zone.cx} y={zone.cy + 16} textAnchor="middle"
                    fontSize={12} fontWeight="700" fill="#fff" fontFamily="Roboto Mono,monospace"
                    style={textShadowStyle}>
                    {displayValue !== null ? `${(displayValue || 0).toFixed(0)} W` : '0 W'}
                </text>
            </g>
        );
    }

    return (
        <g>
            {isFault && (
                <rect x={zone.x - 4} y={zone.y - 4} width={zone.w + 8} height={zone.h + 8} rx={4}
                    fill={fill} opacity={0.12}>
                    <animate attributeName="opacity" values="0.12;0.25;0.12" dur="1.4s" repeatCount="indefinite" />
                </rect>
            )}
            <rect x={zone.x} y={zone.y} width={zone.w} height={zone.h} rx={3}
                fill={fill} stroke={stroke} strokeWidth={3}
                style={{ transition: 'fill 0.6s ease, stroke 0.6s ease' }} />
            <text x={zone.x + zone.w / 2} y={zone.y + zone.h / 2 - 8} textAnchor="middle"
                fontSize={13} fontWeight="800" fill="#fff" fontFamily="Inter,sans-serif"
                style={textShadowStyle}>
                {zone.label}
            </text>
            {liveValue !== null && (
                <text x={zone.x + zone.w / 2} y={zone.y + zone.h / 2 + 14} textAnchor="middle"
                    fontSize={12} fontWeight="700" fill="#fff" fontFamily="Roboto Mono,monospace"
                    style={textShadowStyle}>
                    {liveValue.toFixed(0)} W
                </text>
            )}
            <rect x={zone.x + zone.w - 90} y={zone.y + 8} width={80} height={20} rx={10}
                fill="rgba(255,255,255,0.25)" />
            <text x={zone.x + zone.w - 50} y={zone.y + 22} textAnchor="middle"
                fontSize={9} fontWeight="700" fill="#fff" fontFamily="Inter,sans-serif">
                {STATUS_LABEL[effectiveStatus]}
            </text>
        </g>
    );
}

const TwinMap = React.memo(function TwinMap({ currentRecord, zoneStatuses, streetlightStatuses, timestampLabel, tempLabel }) {
    return (
        <svg viewBox="0 0 940 540" style={{ width: '100%', height: '100%', display: 'block', borderRadius: '16px' }}>
            <rect width="940" height="540" fill="#ffffff" />

            {/* River */}
            <path d="M 480,0 Q 460,180 440,260 Q 410,380 380,540"
                fill="none" stroke="#38bdf8" strokeWidth={18} strokeLinecap="round" />
            <path d="M 480,0 Q 460,180 440,260 Q 410,380 380,540"
                fill="none" stroke="#7dd3fc" strokeWidth={12} opacity={0.2} strokeDasharray="12 4">
                <animate attributeName="stroke-dashoffset" values="0;40" dur="1s" repeatCount="indefinite" />
            </path>

            {/* Road */}
            <rect x={0} y={230} width={940} height={60} fill="#1a1a1a" />
            {Array.from({ length: 20 }).map((_, i) => (
                <rect key={`lt-${i}`} x={10 + i * 48} y={247} width={28} height={5} rx={1} fill="#facc15" />
            ))}
            {Array.from({ length: 14 }).map((_, i) => (
                <rect key={`lb-${i}`} x={10 + i * 48} y={270} width={28} height={5} rx={1} fill="#1a1a1a" opacity={0.5} />
            ))}

            {/* Zones */}
            {ZONES.map(zone => {
                const status = zoneStatuses[zone.id] || 'ok';
                const liveValue = (zone.dataKey && currentRecord)
                    ? (currentRecord[zone.dataKey] ?? null) : null;
                return <BuildingZone key={zone.id} zone={zone} status={status} liveValue={liveValue} />;
            })}

            {/* Streetlights */}
            {STREETLIGHTS.map((sl, i) => (
                <StreetLight key={`sl-${i}`} x={sl.x} y={sl.y} status={streetlightStatuses[i] || 'ok'} />
            ))}

            {/* Legend */}
            <rect x={14} y={498} width={250} height={34} rx={8} fill="rgba(0,0,0,0.06)" stroke="rgba(0,0,0,0.1)" strokeWidth={1} />
            {[
                { color: STATUS_FILL.ok, label: 'Normal Operation' },
                { color: STATUS_FILL.fault, label: 'Anomaly Detected' },
            ].map((item, i) => (
                <g key={item.label} transform={`translate(${24 + i * 120}, 510)`}>
                    <rect x={0} y={0} width={12} height={12} rx={3} fill={item.color} />
                    <text x={17} y={10} fontSize={9} fontWeight="600" fill="#374151" fontFamily="Inter,sans-serif">
                        {item.label}
                    </text>
                </g>
            ))}

            {/* Timestamp overlay */}
            <rect x={680} y={498} width={250} height={34} rx={8} fill="rgba(0,0,0,0.06)" stroke="rgba(0,0,0,0.1)" strokeWidth={1} />
            <text x={695} y={520} fontSize={10} fontWeight="700" fill="#374151" fontFamily="Roboto Mono, monospace">
                {timestampLabel}
            </text>
            <text x={880} y={520} fontSize={9} fontWeight="600"
                fill={currentRecord.temperature < 0 ? '#3b82f6' : '#f97316'}
                fontFamily="Roboto Mono, monospace">
                {tempLabel}
            </text>
        </svg>
    );
});

const SyncedChart = React.memo(function SyncedChart({ data, labels, currentIndex, title, dataKeys, colors, height = 140 }) {
    const startIdx = Math.max(0, currentIndex - 47);
    const endIdx = currentIndex + 1;
    const displayData = useMemo(() => {
        const slice = [];
        for (let i = startIdx; i < endIdx; i++) {
            slice.push({ ...data[i], label: labels[i] });
        }
        return slice;
    }, [data, labels, startIdx, endIdx]);

    return (
        <Box sx={{
            bgcolor: '#111f35', borderRadius: '14px', border: '1px solid rgba(255,255,255,0.06)',
            p: 2, height: '100%',
        }}>
            <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 1 }}>
                <Typography variant="caption" sx={{
                    color: '#7a97bc', fontWeight: 700, textTransform: 'uppercase', letterSpacing: '0.08em', fontSize: 10
                }}>
                    {title}
                </Typography>
            </Box>
            <ResponsiveContainer width="100%" height={height}>
                <AreaChart data={displayData}>
                    <defs>
                        {dataKeys.map((key, i) => (
                            <linearGradient key={key} id={`grad-${key}`} x1="0" y1="0" x2="0" y2="1">
                                <stop offset="0%" stopColor={colors[i]} stopOpacity={0.3} />
                                <stop offset="100%" stopColor={colors[i]} stopOpacity={0.02} />
                            </linearGradient>
                        ))}
                    </defs>
                    <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.04)" />
                    <XAxis dataKey="label" tick={{ fill: '#3f5878', fontSize: 9 }} interval="preserveStartEnd" />
                    <YAxis tick={{ fill: '#3f5878', fontSize: 9 }} width={45} />
                    <RechartsTooltip contentStyle={chartTooltipStyle} />
                    {dataKeys.map((key, i) => (
                        <Area key={key} type="monotone" dataKey={key}
                            stroke={colors[i]} strokeWidth={2}
                            fill={`url(#grad-${key})`}
                            dot={false}
                            isAnimationActive={false} />
                    ))}
                </AreaChart>
            </ResponsiveContainer>
        </Box>
    );
});

export default function DigitalTwin({ currentIndex = 0 }) {
    const currentRecord = ALL_RECORDS[currentIndex] || ALL_RECORDS[0];

    const zoneStatuses = useMemo(() => {
        const statuses = {};
        const currentTime = ALL_EPOCHS[currentIndex];
        const faultZones = new Set();

        for (let i = 0; i < FAULT_RANGES.length; i++) {
            const fr = FAULT_RANGES[i];
            if (currentTime >= fr.start && currentTime <= fr.end) {
                faultZones.add(fr.zone);
            }
        }

        for (let i = 0; i < ZONES.length; i++) {
            const zone = ZONES[i];
            if (zone.disabled) {
                statuses[zone.id] = 'disabled';
            } else {
                statuses[zone.id] = faultZones.has(zone.id) ? 'fault' : 'ok';
            }
        }
        return statuses;
    }, [currentIndex]);

    const streetlightStatuses = useMemo(() => {
        const r = currentRecord;
        return STREETLIGHTS.map(sl => {
            let power;
            switch (sl.zone) {
                case 'Industrial': power = r.indStreet; break;
                case 'Residential': power = r.resStreet; break;
                case 'Stadium': power = 0; break;
                case 'Factory': power = (r.resStreet + r.indStreet) / 2; break;
                case 'Substation': power = r.indStreet * 0.5; break;
                default: power = 1; break;
            }
            return getStreetlightStatus(power);
        });
    }, [currentRecord]);

    const faultCount = Object.values(zoneStatuses).filter(v => v === 'fault').length;
    const timestampLabel = ALL_TIMESTAMPS_LONG[currentIndex] || '';
    const tempLabel = `${currentRecord.temperature.toFixed(1)}°C`;

    return (
        <Box>
            <Box sx={{
                borderRadius: '20px', border: '1px solid rgba(255,255,255,0.08)',
                bgcolor: '#fff', overflow: 'hidden',
                boxShadow: '0 4px 60px rgba(0,0,0,0.4)',
            }}>
                <TwinMap
                    currentRecord={currentRecord}
                    zoneStatuses={zoneStatuses}
                    streetlightStatuses={streetlightStatuses}
                    timestampLabel={timestampLabel}
                    tempLabel={tempLabel}
                />
            </Box>

            <Box sx={{ mt: 2, display: 'grid', gridTemplateColumns: { xs: '1fr', md: '1fr 1fr' }, gap: 2 }}>
                <SyncedChart
                    data={ALL_RECORDS}
                    labels={ALL_LABELS}
                    currentIndex={currentIndex}
                    title="Total Grid Load (W)"
                    dataKeys={['totalGrid']}
                    colors={['#38bdf8']}
                />
                <SyncedChart
                    data={ALL_RECORDS}
                    labels={ALL_LABELS}
                    currentIndex={currentIndex}
                    title="Residential Consumption (W)"
                    dataKeys={['residential']}
                    colors={['#a78bfa']}
                />
                <SyncedChart
                    data={ALL_RECORDS}
                    labels={ALL_LABELS}
                    currentIndex={currentIndex}
                    title="Industrial Consumption (W)"
                    dataKeys={['industrial']}
                    colors={['#4ade80']}
                />
                <SyncedChart
                    data={ALL_RECORDS}
                    labels={ALL_LABELS}
                    currentIndex={currentIndex}
                    title="Temperature & Humidity"
                    dataKeys={['temperature', 'humidity']}
                    colors={['#fb923c', '#38bdf8']}
                />
            </Box>

        </Box>
    );
}

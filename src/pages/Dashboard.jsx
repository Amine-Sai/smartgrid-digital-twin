import React, { useState, useEffect, useRef } from 'react';
import { Box } from '@mui/material';
import DigitalTwin, { ALL_RECORDS, TOTAL_HOURS } from './DigitalTwin';

const MAX_RES = Math.max(...ALL_RECORDS.map(r => r.residential));
const MAX_FACTORY = Math.max(...ALL_RECORDS.map(r => r.factory));
const MAX_IND = Math.max(...ALL_RECORDS.map(r => r.industrial));

const Dashboard = () => {
    const iframeRef = useRef(null);
    const [currentIndex, setCurrentIndex] = useState(0);
    const [iframeHeight, setIframeHeight] = useState(1200);

    useEffect(() => {
        const handleMessage = (event) => {
            if (event.data && event.data.type === 'FRAME_RESIZE' && event.data.height) {
                setIframeHeight(Math.max(1000, event.data.height + 20));
            }
        };
        window.addEventListener('message', handleMessage);
        return () => window.removeEventListener('message', handleMessage);
    }, []);

    useEffect(() => {
        const id = setInterval(() => {
            setCurrentIndex(prev => (prev >= TOTAL_HOURS - 1 ? 0 : prev + 1));
        }, 1000);
        return () => clearInterval(id);
    }, []);

    useEffect(() => {
        const currentRecord = ALL_RECORDS[currentIndex];
        const iframeWin = iframeRef.current?.contentWindow;
        if (!iframeWin || !currentRecord) return;

        iframeWin.postMessage({ topic: 'smartgrid/power', msg: currentRecord.totalGrid.toString() }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/voltage', msg: '12.0' }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/current', msg: (currentRecord.totalGrid / 12).toString() }, '*');

        iframeWin.postMessage({ topic: 'smartgrid/meteo/temperature', msg: currentRecord.temperature.toString() }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/meteo/humidite', msg: currentRecord.humidity.toString() }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/meteo/pression', msg: '1013.2' }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/meteo/gaz', msg: '45.0' }, '*');

        const hour = currentRecord.hour;
        const isNight = hour < 6 || hour > 19;
        iframeWin.postMessage({ topic: 'smartgrid/ldr', msg: isNight ? '3000' : '500' }, '*');

        const led1Duty = Math.min(100, Math.max(0, (currentRecord.residential / MAX_RES) * 100));
        const led2Duty = Math.min(100, Math.max(0, (currentRecord.factory / MAX_FACTORY) * 100));
        const moteurDuty = Math.min(100, Math.max(0, (currentRecord.industrial / MAX_IND) * 100));

        iframeWin.postMessage({ topic: 'smartgrid/led1/duty', msg: led1Duty.toFixed(0) }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/led1/state', msg: currentRecord.residential > 0 ? 'ON' : 'OFF' }, '*');

        iframeWin.postMessage({ topic: 'smartgrid/led2/duty', msg: led2Duty.toFixed(0) }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/led2/state', msg: currentRecord.factory > 0 ? 'ON' : 'OFF' }, '*');

        iframeWin.postMessage({ topic: 'smartgrid/moteur/duty', msg: moteurDuty.toFixed(0) }, '*');
        iframeWin.postMessage({ topic: 'smartgrid/moteur/state', msg: currentRecord.industrial > 0 ? 'ON' : 'OFF' }, '*');
    }, [currentIndex]);

    return (
        <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <DigitalTwin currentIndex={currentIndex} />

            <Box sx={{
                borderRadius: '16px',
                border: '1px solid rgba(255,255,255,0.08)',
                overflow: 'hidden',
                width: '100%',
            }}>
                <iframe
                    ref={iframeRef}
                    src="/admin-v3.html"
                    scrolling="no"
                    style={{ width: '100%', height: `${iframeHeight}px`, border: 'none', overflow: 'hidden' }}
                    title="Smart Grid Admin Dashboard"
                />
            </Box>
        </Box>
    );
};

export default Dashboard;

import React from 'react';
import { Box, AppBar, Toolbar, Typography } from '@mui/material';

const Layout = ({ children }) => {
    return (
        <Box sx={{ display: 'flex', flexDirection: 'column', width: '100%', height: '100vh', bgcolor: 'background.default', overflow: 'hidden' }}>
            {/* Top bar */}
            <AppBar position="sticky" sx={{ zIndex: (t) => t.zIndex.drawer + 1 }}>
                <Toolbar variant="dense" sx={{ gap: 2, minHeight: 56, py: 0.5, px: 3 }}>
                    <Box sx={{ flex: 1 }}>
                        <Typography variant="caption" sx={{ color: '#7a9f0f6ff7bc', fontSize: '1.5rem', fontWeight: 400 }}>
                            Intelligent Energy Management System
                        </Typography>
                    </Box>
                </Toolbar>
            </AppBar>

            {/* Page content */}
            <Box sx={{ flex: 1, px: { xs: 1.5, sm: 2.5 }, py: 1.5, overflowY: 'auto', scrollbarWidth: 'none', '&::-webkit-scrollbar': { display: 'none' } }}>
                {children}
            </Box>
        </Box>
    );
};

export default Layout;
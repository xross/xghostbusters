#include <platform.h>
#include <sw_pll.h>
#include <mic_array.h>
#include <xcore/assert.h>
#include "app.h"

void microphone_init(void) {
    // The application PLL drives X1D11; its port input provides clock feedback
    // internally, requiring no external loopback wire.
    xassert(sw_pll_fixed_clock(24576000, SW_PLL_TILE_1) == SW_PLL_SUCCESS);
    pdm_rx_resources_t resources = PDM_RX_RESOURCES_DDR(
        PORT_MCLK, PORT_PDM_CLK, PORT_PDM_DATA, 24576000, 3072000,
        XS1_CLKBLK_1, XS1_CLKBLK_2);
    mic_array_init(&resources, NULL, 16000);
}

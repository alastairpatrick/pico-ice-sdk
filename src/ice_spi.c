#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "boards/pico_ice.h"
#include "ice_spi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

volatile bool g_transfer_done = true;
volatile void (*g_async_callback)(volatile void *);
volatile void *g_async_context;

#if 0 // TODO: switch to hardware implementation once ready.
void ice_spi_init(void)
{
    // This driver is only focused on one particular SPI bus
    gpio_set_function(ICE_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ICE_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ICE_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_dir(ICE_DEFAULT_SPI_SCK_PIN, GPIO_OUT);
    gpio_set_dir(ICE_DEFAULT_SPI_TX_PIN, GPIO_OUT);
    gpio_set_dir(ICE_DEFAULT_SPI_RX_PIN, GPIO_IN);

    // It configures a single SPI instance
    spi_init(ICE_DEFAULT_SPI);
}
#else
void ice_spi_init(void)
{
    // This driver is only focused on one particular SPI bus
    gpio_init(ICE_DEFAULT_SPI_SCK_PIN);
    gpio_init(ICE_DEFAULT_SPI_TX_PIN);
    gpio_init(ICE_DEFAULT_SPI_RX_PIN);

    // Everything in high impedance before a transaction occurs
    gpio_set_dir(ICE_DEFAULT_SPI_SCK_PIN, GPIO_IN);
    gpio_set_dir(ICE_DEFAULT_SPI_TX_PIN, GPIO_IN);
    gpio_set_dir(ICE_DEFAULT_SPI_RX_PIN, GPIO_IN);
}
#endif

static uint8_t transfer_byte(uint8_t tx)
{
    uint8_t rx;

    for (uint8_t i = 0; i < 8; i++) {
        // Update TX and immediately set negative edge.
        gpio_put(ICE_DEFAULT_SPI_SCK_PIN, false);
        gpio_put(ICE_DEFAULT_SPI_TX_PIN, tx >> 7);
        tx <<= 1;

        // stable for a while with clock low
        sleep_us(1);

        // Sample RX as we set positive edge.
        rx <<= 1;
        rx |= gpio_get(ICE_DEFAULT_SPI_RX_PIN);
        gpio_put(ICE_DEFAULT_SPI_SCK_PIN, true);

        // stable for a while with clock high
        sleep_us(1);
    }
    return rx;
}

void ice_spi_chip_select(uint8_t csn_pin)
{
    // Request the bus
    gpio_put(ICE_DEFAULT_SPI_SCK_PIN, false);
    gpio_put(ICE_DEFAULT_SPI_TX_PIN, true);
    gpio_set_dir(ICE_DEFAULT_SPI_SCK_PIN, GPIO_OUT);
    gpio_set_dir(ICE_DEFAULT_SPI_TX_PIN, GPIO_OUT);

    // Assert CSN for to select the chip
    gpio_put(csn_pin, false);
    gpio_set_dir(csn_pin, GPIO_OUT);
    sleep_us(1);
}

void ice_spi_chip_deselect(uint8_t csn_pin)
{
    gpio_put(csn_pin, false);
    sleep_us(1);
    gpio_put(csn_pin, true);
    sleep_us(1);;

    // Release the bus including CSN
    gpio_set_dir(ICE_DEFAULT_SPI_SCK_PIN, GPIO_IN);
    gpio_set_dir(ICE_DEFAULT_SPI_TX_PIN, GPIO_IN);
    gpio_set_dir(csn_pin, GPIO_IN);
}

static void prepare_transfer(void (*callback)(volatile void *), void *context)
{
    uint32_t status;

    status = save_and_disable_interrupts();
    g_async_callback = callback;
    g_async_context = context;
    g_transfer_done = false;
    restore_interrupts(status);
}

// TODO: this is not yet called from interrupt yet
static void spi_irq_handler(void)
{
    if (true) { // TODO: check for pending bytes to transfer
        if (g_async_callback != NULL) {
            (*g_async_callback)(g_async_context);
        }
        g_transfer_done = true;
    }
}

void ice_spi_write_async(uint8_t const *buf, size_t len, void (*callback)(volatile void *), void *context)
{
    // TODO: we could just call callback(context) directly but this is to mimick the future behavior
    prepare_transfer(callback, context);

    for (; len > 0; len--, buf++) {
        transfer_byte(*buf);
    }
    spi_irq_handler();
}

void ice_spi_read_async(uint8_t tx, uint8_t *buf, size_t len, void (*callback)(volatile void *), void *context)
{
    // TODO: we could just call callback(context) directly but this is to mimick the future behavior
    prepare_transfer(callback, context);

    for (; len > 0; len--, buf++) {
        *buf = transfer_byte(tx);
    }
    spi_irq_handler();
}

bool ice_spi_is_async_complete(void)
{
    return g_transfer_done;
}

void ice_spi_await_async_completion(void)
{
    while (!ice_spi_is_async_complete()) {
        // _WFE(); // TODO: uncomment this while switching to interrupt-based implementation
    }
}

void ice_spi_read_blocking(uint8_t tx, uint8_t *buf, size_t len)
{
    ice_spi_read_async(tx, buf, len, NULL, NULL);
    ice_spi_await_async_completion();
}

void ice_spi_write_blocking(uint8_t const *buf, size_t len)
{
    ice_spi_write_async(buf, len, NULL, NULL);
    ice_spi_await_async_completion();
}

void ice_spi_request_bus(void)
{
}

void ice_spi_release_bus(void)
{
}

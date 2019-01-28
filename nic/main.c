#include <stdio.h>
#define ETHOCAN_TIMEOUT_USEC (500);
#include "ethocan.h"
#include "xtimer.h"
#include "board.h"
#include "periph/gpio.h"
#include "periph/uart.h"
#include "net/netdev.h"
#include "net/ethernet.h"

#define HOSTTIMEOUT_US    (15000)
#define HOSTBAUDRATE      (115200)
#define HOSTUART          UART_DEV(0)
#define HOSTRDY           GPIO_PIN(PORT_A, 8)
#define NETBAUDRATE       (500000)
#define NETUART           UART_DEV(2)
#define NETSENSE          GPIO_PIN(PORT_B, 1)

#define MSG_QUEUE_SIZE    (8)
#define EVENT_CALL_ISR    (0x4141)
#define EVENT_SEND        (0x4242)

typedef struct buf {
    network_uint16_t hdr;
    uint8_t data[ETHERNET_FRAME_LEN];
} buf_t;

typedef struct bridge {
    uart_t uart;
    gpio_t rdy;
    kernel_pid_t pid;
    buf_t inbuf;
    uint16_t inbuf_len;
    uint8_t inbuf_dirty;
    buf_t outbuf;
    xtimer_t timeout;
    uint32_t timeout_ticks;
} bridge_t;

static void _event_cb(netdev_t *dev, netdev_event_t event)
{
    bridge_t *ctx = dev->context;
    if (event == NETDEV_EVENT_ISR) {
        /* CALLED FROM ISR CONEXT */
        msg_t msg = { .type = EVENT_CALL_ISR };

        if (msg_send(&msg, ctx->pid) <= 0) {
            puts("gnrc_netif: possibly lost interrupt.");
        }
    }
    else if (event == NETDEV_EVENT_RX_COMPLETE) {
        /* CALLED FROM THREAD CONTEXT */

        /* Get packet length */
        int pktlen = dev->driver->recv(dev, NULL, 0, NULL);
        if (pktlen <= 0) {
            return;
        }
        if (pktlen > (int) ETHERNET_FRAME_LEN || pktlen < (int) sizeof(ethernet_hdr_t)) {
            /* drop packet */
            printf("drop %d > %d\n", pktlen, ETHERNET_FRAME_LEN);
            dev->driver->recv(dev, NULL, pktlen, NULL);
            return;
        }

        /* Fill outbuffer */
        pktlen = dev->driver->recv(dev, ctx->outbuf.data, pktlen, NULL);
        ctx->outbuf.hdr = byteorder_htons((uint16_t) pktlen);

        /* Send buffer to host */
        uart_write(ctx->uart, (uint8_t *) &ctx->outbuf, pktlen + 2);
    }
}

static void _isr_uart(void* args, uint8_t c)
{
    netdev_t *dev = args;
    bridge_t *ctx = dev->context;
    uint8_t *data = (uint8_t *) &ctx->inbuf;

    if (ctx->inbuf_dirty || ctx->inbuf_len > sizeof(buf_t)) {
        /* The buffer contains a complete frame or is full */
        return;
    }

    /* Store the byte */
    data[ctx->inbuf_len++] = c;

    /* Retrigger the timeout ... it will clear the buffer if the
     * host falls asleep while transmitting a frame */
    xtimer_set(&ctx->timeout, ctx->timeout_ticks);

    if (ctx->inbuf_len <= 2) return;
    if (ctx->inbuf_len >= byteorder_ntohs(ctx->inbuf.hdr) + 2) {
        /* We collected enough bytes ... queue transmission */
        msg_t msg = { .type = EVENT_SEND };
        msg_send(&msg, ctx->pid);
        ctx->inbuf_dirty = 1;
        gpio_set(ctx->rdy);
    }
}

static void _isr_xtimer(void *args)
{
    netdev_t *dev = args;
    bridge_t *ctx = dev->context;

    /* Roll-back everything */
    ctx->inbuf_len = 0;
    ctx->inbuf_dirty = 0;
    gpio_clear(ctx->rdy);
}

void setup(ethocan_t *ctx, bridge_t *bridge)
{
    ethocan_params_t p;
    p.uart = NETUART;
    p.baudrate = NETBAUDRATE;
    p.sense_pin = NETSENSE;
    ethocan_setup(ctx, &p);
    ctx->netdev.event_callback = _event_cb;
    ctx->netdev.context = (void *) bridge;

    bridge->pid = thread_getpid();
    bridge->uart = HOSTUART;
    uart_init(bridge->uart, HOSTBAUDRATE, _isr_uart, (void *) ctx);
    bridge->rdy = HOSTRDY;
    gpio_init(bridge->rdy, GPIO_OUT);
    bridge->inbuf_len = 0;
    bridge->timeout_ticks = xtimer_ticks_from_usec(HOSTTIMEOUT_US).ticks32;
    bridge->timeout.callback = _isr_xtimer;
    bridge->timeout.arg = ctx;
}

void run(netdev_t * dev)
{
    bridge_t *ctx = dev->context;
    msg_t msg, msg_queue[MSG_QUEUE_SIZE];
    netopt_enable_t en = NETOPT_ENABLE;

    msg_init_queue(msg_queue, MSG_QUEUE_SIZE);
    dev->driver->init(dev);

    /* Make sure to forward every frame ...
     * The host decides whether to process them or not */
    dev->driver->set(dev, NETOPT_PROMISCUOUSMODE, &en, sizeof(en));

    while (1) {
        msg_receive(&msg);
        if (msg.type == EVENT_CALL_ISR) {
            puts("EVENT_CALL_ISR");
            dev->driver->isr(dev);
        }
        else if (msg.type == EVENT_SEND && ctx->inbuf_dirty) {
            /* Clear timeout started by last sent octet */
            xtimer_remove(&ctx->timeout);

            /* Send packet onto the bus */
            iolist_t iolist = {
                .iol_next = NULL,
                .iol_base = ctx->inbuf.data,
                .iol_len = ctx->inbuf_len - 2
            };
            printf("EVENT_SEND %d\n", dev->driver->send(dev, &iolist));

            /* Clear buffer */
            int irq_state = irq_disable();
            ctx->inbuf_len = 0;
            ctx->inbuf_dirty = 0;
            gpio_clear(ctx->rdy);
            irq_restore(irq_state);
        }
    }
}

int main(void) {
    static ethocan_t ethocan;
    static bridge_t bridge;

    puts("setup");
    setup(&ethocan, &bridge);

    printf("run %d\n", ETHERNET_FRAME_LEN);
    run((netdev_t *) &ethocan);

    return 0;
}

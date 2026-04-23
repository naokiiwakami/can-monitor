#![no_std]
#![no_main]

use core::fmt::Write;
use defmt::{debug, error};
use embassy_executor::Spawner;
use embassy_futures::select::{Either, select};
use embassy_stm32::can::{
    self, Can,
    frame::{Frame, Header},
};
use embassy_stm32::gpio::{Input, Level, Output, Pull, Speed};
use embassy_stm32::mode;
use embassy_stm32::peripherals;
#[cfg(feature = "hsi")]
use embassy_stm32::rcc::{Hsi, HsiKerDiv, HsiSysDiv};
use embassy_stm32::usart::{self, Uart};
use embassy_stm32::{Peripherals, bind_interrupts};
#[cfg(feature = "hse")]
use embassy_stm32::{
    rcc::{Hse, HseMode, Sysclk},
    time::Hertz,
};
use embassy_sync::{
    blocking_mutex::raw::ThreadModeRawMutex,
    channel::{Channel, Sender},
};
use embedded_can::Id;
use heapless::String;
use {defmt_rtt as _, panic_probe as _};
use {embassy_stm32::can::frame::FdFrame, embedded_can::StandardId};

static RX_CHANNEL: Channel<ThreadModeRawMutex, Frame, 8> = Channel::new();
static TX_CHANNEL: Channel<ThreadModeRawMutex, FdFrame, 2> = Channel::new();

bind_interrupts!(struct CanIrqs {
    FDCAN1_IT0 => can::IT0InterruptHandler<peripherals::FDCAN1>;
    FDCAN1_IT1 => can::IT1InterruptHandler<peripherals::FDCAN1>;
});

bind_interrupts!(struct UsartIrqs {
    USART1 => usart::InterruptHandler<peripherals::USART1>;
    DMA1_CHANNEL2_3 =>
        embassy_stm32::dma::InterruptHandler<peripherals::DMA1_CH2>,
        embassy_stm32::dma::InterruptHandler<peripherals::DMA1_CH3>;
});

async fn display_frame(
    rx_frame: Frame,
    usart: &mut Uart<'static, mode::Async>,
) -> Result<(), usart::Error> {
    let mut log_message: String<128> = String::new();
    let fd = if rx_frame.header().fdcan() { "f" } else { "c" };
    let brs = if rx_frame.header().bit_rate_switching() {
        "b"
    } else {
        "-"
    };
    match rx_frame.id() {
        Id::Standard(id) => {
            write!(log_message, "({}{}) std [ {:03x} ]:", fd, brs, id.as_raw()).unwrap()
        }
        Id::Extended(id) => {
            write!(log_message, "({}{}) ext [ {:08x} ]:", fd, brs, id.as_raw()).unwrap()
        }
    };
    for &value in &rx_frame.data()[0..rx_frame.header().len() as usize] {
        write!(log_message, " {:02x}", value).unwrap();
    }
    write!(log_message, "\r\n").unwrap();
    return usart.write(log_message.as_bytes()).await;
}

async fn process_command(
    command_line: &String<128>,
    tx_sender: &mut Sender<'static, ThreadModeRawMutex, FdFrame, 2>,
    usart: &mut Uart<'static, mode::Async>,
) {
    match command_line.as_str() {
        "help" => {
            usart
                .write(b"Available commands: tx, txfd, help\r\n")
                .await
                .unwrap();
        }

        "tx" => {
            // usart.write(b"pong\r\n").await.unwrap();
            let frame = can::frame::FdFrame::new_standard(0x6ad, &[0x41, 0x26]).unwrap();
            tx_sender.send(frame).await;
        }

        "txfd" => {
            let data: [u8; 7] = [0x8a, 0xd1, 0x0a, 0xc7, 0x1b, 0x17, 0xee];
            let header = Header::new_fd(
                Id::Standard(StandardId::new(0x170).unwrap()),
                data.len() as u8,
                false,
                true,
            );

            let frame = FdFrame::new(header, &data).unwrap();
            tx_sender.send(frame).await;
        }

        "" => {}

        _ => {
            usart.write(b"Unknown command\r\n").await.unwrap();
        }
    }
}

#[embassy_executor::task]
async fn message_consumer(mut usart: Uart<'static, mode::Async>) {
    usart
        .write(b"\r\n******************************\r\n")
        .await
        .unwrap();
    usart.write(b"  CAN Bus Monitor\r\n").await.unwrap();
    usart
        .write(b"******************************\r\n\r\n")
        .await
        .unwrap();

    let rx_receiver = RX_CHANNEL.receiver();
    let mut tx_sender = TX_CHANNEL.sender();
    let mut buf = [0u8; 1];
    let mut command_line: String<128> = String::new();
    loop {
        match select(usart.read(&mut buf), rx_receiver.receive()).await {
            Either::First(out) => {
                if out.is_ok() {
                    match &buf[0] {
                        b'\r' => {
                            let temp_buf = b"\r\n";
                            usart.write(temp_buf).await.unwrap();
                            process_command(&command_line, &mut tx_sender, &mut usart).await;
                            command_line.clear();
                        }
                        _ => {
                            usart.write(&buf).await.unwrap();
                            write!(command_line, "{}", buf[0] as char).unwrap();
                        }
                    };
                }
            }
            Either::Second(rx_frame) => display_frame(rx_frame, &mut usart).await.unwrap(),
        };
    }
}

fn init() -> Peripherals {
    let mut config = embassy_stm32::Config::default();

    // configure system clock
    #[cfg(feature = "hse")]
    {
        // Set system clock source to HSE with 24 MHz crystal
        config.rcc.hse = Some(Hse {
            freq: Hertz::mhz(48),
            mode: HseMode::Oscillator,
        });
        config.rcc.sys = Sysclk::Hse;
    }

    #[cfg(feature = "hsi")]
    {
        // set system clock source to HSI with 48 MHz RC oscillation
        config.rcc.hsi = Some(Hsi {
            sys_div: HsiSysDiv::Div1,
            ker_div: HsiKerDiv::Div1,
        });
    }

    embassy_stm32::init(config)
}

fn setup_peripherals(
    p: Peripherals,
) -> (
    Can<'static>,
    Output<'static>,
    Uart<'static, mode::Async>,
    Output<'static>,
    Input<'static>,
) {
    let usart = Uart::new(
        p.USART1,
        p.PA8,
        p.PA9,
        p.DMA1_CH2,
        p.DMA1_CH3,
        UsartIrqs,
        usart::Config::default(),
    )
    .unwrap();

    // can standby controller pin
    let mut can_stb = Output::new(p.PB4, Level::High, Speed::Low);
    let debug_out = Output::new(p.PB1, Level::Low, Speed::Low);
    let rate_select = Input::new(p.PB0, Pull::Up);

    // disable the CAN transceiver
    can_stb.set_high();

    // start the CAN controller
    let can = {
        let mut can_config = can::CanConfigurator::new(p.FDCAN1, p.PB5, p.PB6, CanIrqs);
        let nominal_bitrate = 1_000_000;
        let data_bitrate = 4_000_000;
        debug!(
            "Starting CAN FD with nominal bitrate {} mbps, data bitrate {} mbps",
            nominal_bitrate / 1_000_000,
            data_bitrate / 1_000_000
        );
        can_config.set_bitrate(nominal_bitrate);
        can_config.set_fd_data_bitrate(data_bitrate, true);
        can_config.into_normal_mode()
    };

    // enable the CAN transceiver
    can_stb.set_low();

    (can, can_stb, usart, debug_out, rate_select)
}

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = init();
    let (mut can, mut _can_stb, usart, mut _debug_out, _rate_select) = setup_peripherals(p);

    _spawner.spawn(message_consumer(usart).unwrap());

    let rx_sender = RX_CHANNEL.sender();
    let tx_receiver = TX_CHANNEL.receiver();
    loop {
        /*
        _debug_out.toggle();
        Timer::after_millis(500).await;
        */
        match select(can.read(), tx_receiver.receive()).await {
            Either::First(read_result) => match read_result {
                Ok(envelope) => {
                    rx_sender.send(envelope.frame).await;
                }
                Err(err) => {
                    error!("Error in frame: {:?}", err);
                    break;
                }
            },
            Either::Second(tx_frame) => {
                _ = can.write_fd(&tx_frame).await;
            }
        }
    }
}

#![no_std]
#![no_main]

use core::fmt::Write;
use defmt::error;
use embassy_executor::Spawner;
use embassy_stm32::can::{self, Can, frame::Frame};
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
use embassy_sync::{blocking_mutex::raw::ThreadModeRawMutex, channel::Channel};
use embedded_can::Id;
use heapless::String;
use {defmt_rtt as _, panic_probe as _};
#[cfg(feature = "bit-rate-switching")]
use {
    embassy_stm32::can::frame::{FdFrame, Header},
    embedded_can::StandardId,
};

static CHANNEL: Channel<ThreadModeRawMutex, Frame, 8> = Channel::new();

bind_interrupts!(struct CanIrqs {
    FDCAN1_IT0 => can::IT0InterruptHandler<peripherals::FDCAN1>;
    FDCAN1_IT1 => can::IT1InterruptHandler<peripherals::FDCAN1>;
});

bind_interrupts!(struct UsartIrqs {
    USART1 => usart::InterruptHandler<peripherals::USART1>;
});

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

    let receiver = CHANNEL.receiver();
    loop {
        let rx_frame = receiver.receive().await;
        let mut log_message: String<128> = String::new();
        match rx_frame.id() {
            Id::Standard(id) => write!(log_message, "std [ {:03x} ]:", id.as_raw()).unwrap(),
            Id::Extended(id) => write!(log_message, "ext [ {:08x} ]:", id.as_raw()).unwrap(),
        };
        for &value in &rx_frame.data()[0..rx_frame.header().len() as usize] {
            write!(log_message, " {:02x}", value).unwrap();
        }
        write!(log_message, "\r\n").unwrap();
        usart.write(log_message.as_bytes()).await.unwrap();
    }
}

fn init() -> Peripherals {
    let mut config = embassy_stm32::Config::default();

    // configure system clock
    #[cfg(feature = "hse")]
    {
        // Set system clock source to HSE with 24 MHz crystal
        config.rcc.hse = Some(Hse {
            freq: Hertz::mhz(24),
            mode: HseMode::Oscillator,
        });
        config.rcc.sys = Sysclk::HSE;
    }

    #[cfg(feature = "hsi")]
    {
        // set system clock source to HSI with 48 MHz RC oscillation
        config.rcc.hsi = Some(Hsi {
            sys_div: HsiSysDiv::DIV1,
            ker_div: HsiKerDiv::DIV1,
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
        UsartIrqs,
        p.DMA1_CH2,
        p.DMA1_CH3,
        usart::Config::default(),
    )
    .unwrap();

    // can standby controller pin
    let mut can_stb = Output::new(p.PB4, Level::High, Speed::Low);
    let mut debug_out = Output::new(p.PB1, Level::Low, Speed::Low);
    let rate_select = Input::new(p.PB0, Pull::Up);

    // disable the CAN tranceiver
    can_stb.set_high();

    // start the CAN controller
    let can = {
        let mut can_config = can::CanConfigurator::new(p.FDCAN1, p.PB5, p.PB6, CanIrqs);
        can_config.set_bitrate(1_000_000);

        #[cfg(feature = "bit-rate-switching")]
        {
            if rate_select.is_high() {
                can_config.set_fd_data_bitrate(4_000_000, true);
            } else {
                debug_out.set_high();
                can_config.set_fd_data_bitrate(6_000_000, true);
            }
        }

        can_config.into_normal_mode()
    };

    // enable the CAN tranceiver
    can_stb.set_low();

    (can, can_stb, usart, debug_out, rate_select)
}

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = init();
    let (mut can, mut _can_stb, usart, mut _debug_out, _rate_select) = setup_peripherals(p);

    _spawner.spawn(message_consumer(usart).unwrap());

    /*
    {
        let frame = can::frame::FdFrame::new_standard(
            0x170,
            &[0x41, 0x26],
            // &[
            //     0b00010101, 0b00000101, 0b01010101, 0b00010001, 0x00000000, 0b00000000, 0x00000000,
            //     0x00000001,
            // ],
        )
        .unwrap();
        _ = can.write_fd(&frame).await;
    }
    */

    {
        // let data: [u8; 8] = [0x15, 0x05, 0x55, 0x11, 0x00, 0x00, 0x00, 0x01];
        let data: [u8; 2] = [0x41, 0x26];
        let header = Header::new_fd(
            Id::Standard(StandardId::new(0x170).unwrap()),
            data.len() as u8,
            false,
            true,
        );

        let frame = FdFrame::new(header, &data).unwrap();
        _ = can.write_fd(&frame).await;
    }

    let sender = CHANNEL.sender();
    loop {
        match can.read().await {
            Ok(envelope) => {
                sender.send(envelope.frame).await;
            }
            Err(err) => error!("Error in frame: {:?}", err),
        }
    }
}

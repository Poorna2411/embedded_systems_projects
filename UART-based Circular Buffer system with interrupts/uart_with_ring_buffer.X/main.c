#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define F_CPU 16000000UL
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

#define BUFFER_SIZE 64

// Circular buffer variables
volatile char rx_buffer[BUFFER_SIZE];
volatile uint8_t head = 0;
volatile uint8_t tail = 0;

// Function to check if buffer is empty
uint8_t is_buffer_empty() {
    return head == tail;
}

// Function to check if buffer is full
uint8_t is_buffer_full() {
    return ((head + 1) % BUFFER_SIZE) == tail;
}

// UART Initialization
void uart_init(void) {
    // Set baud rate
    UBRR0H = (UBRR_VALUE >> 8);
    UBRR0L = UBRR_VALUE;

    // Enable receiver, transmitter and RX complete interrupt
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);

    // Set frame format: 8 data bits, 1 stop bit
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);

    // Enable global interrupts
    sei();
}

// Read from circular buffer
char uart_read() {
    if (is_buffer_empty())
        return -1; // return -1 if buffer is empty

    char data = rx_buffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return data;
}

// Send a single character
void uart_send(char data) {
    while (!(UCSR0A & (1 << UDRE0))); // Wait until transmit buffer is empty
    UDR0 = data; // Send the data
}

// Echo received data
void uart_echo() {
    if (!is_buffer_empty()) {
        char c = uart_read();
        uart_send(c); // Echo back
    }
}

// ISR for UART Receive
ISR(USART_RX_vect) {
    char data = UDR0; // Read received byte
    uint8_t next_head = (head + 1) % BUFFER_SIZE;

    if (next_head != tail) {
        rx_buffer[head] = data;
        head = next_head;
    }
    // else: buffer overflow, drop the character
}

// Main function
int main(void) {
    uart_init(); // Initialize UART

    while (1) {
        uart_echo(); // Echo back received characters
    }
}


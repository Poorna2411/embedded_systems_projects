#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdint.h>

#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

#define BUFFER_SIZE 64
#define MAX_CMD_LEN 32
#define MAX_LOGS 10
#define TICK_MS 10 // scheduler tick (10ms)

// ========== UART RING BUFFER ==========
volatile char rx_buffer[BUFFER_SIZE];
volatile uint8_t head = 0, tail = 0;

uint8_t buffer_is_empty() { return head == tail; }
uint8_t buffer_is_full() { return ((head + 1) % BUFFER_SIZE) == tail; }

char buffer_read() {
    if (buffer_is_empty()) return -1;
    char c = rx_buffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return c;
}

ISR(USART_RX_vect) {
    char data = UDR0;
    uint8_t next = (head + 1) % BUFFER_SIZE;
    if (next != tail) {
        rx_buffer[head] = data;
        head = next;
    }
}

void uart_init(void) {
    UBRR0H = (UBRR_VALUE >> 8);
    UBRR0L = UBRR_VALUE;
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
    sei();
}

void uart_send(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

void uart_send_str(const char* s) {
    while (*s) uart_send(*s++);
}

// ========== LOG LINKED LIST ==========
struct TaskNode {
    char data[MAX_CMD_LEN];
    uint16_t delay_ticks;
    uint8_t priority;
    struct TaskNode* next;
};

typedef struct TaskNode TaskNode;

TaskNode* log_head = NULL;

void add_log(const char* msg, uint16_t delay_ms, uint8_t priority) {
    TaskNode* new_node = (TaskNode*) malloc(sizeof(TaskNode));
    if (!new_node) return;
    strncpy(new_node->data, msg, MAX_CMD_LEN - 1);
    new_node->data[MAX_CMD_LEN - 1] = '\0';
    new_node->delay_ticks = delay_ms / TICK_MS;
    new_node->priority = priority;
    new_node->next = NULL;

    if (log_head == NULL || priority < log_head->priority) {
        new_node->next = log_head;
        log_head = new_node;
        return;
    }

    TaskNode* temp = log_head;
    while (temp->next != NULL && temp->next->priority <= priority)
        temp = temp->next;

    new_node->next = temp->next;
    temp->next = new_node;
}

void print_log() {
    TaskNode* temp = log_head;
    int count = 1;
    while (temp != NULL) {
        char buf[50];
        snprintf(buf, sizeof(buf), "%d: [%d] %s\n", count++, temp->priority, temp->data);
        uart_send_str(buf);
        temp = temp->next;
    }
}

// ========== EEPROM SAVE/LOAD ==========
uint16_t ee_address = 0;
void save_logs_to_eeprom() {
    TaskNode* temp = log_head;
    while (temp != NULL) {
        eeprom_write_block((const void*)temp, (void*)ee_address, sizeof(TaskNode));
        ee_address += sizeof(TaskNode);
        temp = temp->next;
    }
    uart_send_str("Logs saved to EEPROM.\n");
}

void load_logs_from_eeprom() {
    ee_address = 0;
    for (int i = 0; i < MAX_LOGS; i++) {
        TaskNode* new_node = (TaskNode*) malloc(sizeof(TaskNode));
        if (!new_node) break;
        eeprom_read_block((void*)new_node, (const void*)ee_address, sizeof(TaskNode));
        ee_address += sizeof(TaskNode);
        new_node->next = log_head;
        log_head = new_node;
    }
    uart_send_str("Logs loaded from EEPROM.\n");
}

// ========== TASK RUNNER ==========
void execute_tasks() {
    TaskNode* temp = log_head;
    TaskNode* prev = NULL;

    while (temp != NULL) {
        if (temp->delay_ticks > 0) {
            temp->delay_ticks--;
        } else {
            uart_send_str("Task: ");
            uart_send_str(temp->data);
            uart_send_str("\n");

            if (prev == NULL) {
                log_head = temp->next;
                free(temp);
                temp = log_head;
                continue;
            } else {
                prev->next = temp->next;
                free(temp);
                temp = prev->next;
                continue;
            }
        }
        prev = temp;
        temp = temp->next;
    }
}

// ========== TIMER ISR ==========
volatile uint8_t tick = 0;
ISR(TIMER1_COMPA_vect) { tick = 1; }

void timer_init() {
    TCCR1B |= (1 << WGM12);
    OCR1A = 2499;
    TCCR1B |= (1 << CS11) | (1 << CS10);
    TIMSK1 |= (1 << OCIE1A);
    sei();
}

// ========== COMMAND PARSER ==========
char cmd_buf[MAX_CMD_LEN];
uint8_t idx = 0;

void parse_command(const char* cmd) {
    if (strncmp(cmd, "LED ON", 6) == 0) {
        PORTB |= (1 << PB0);
        uart_send_str("LED ON\n");
    } else if (strncmp(cmd, "LED OFF", 7) == 0) {
        PORTB &= ~(1 << PB0);
        uart_send_str("LED OFF\n");
    } else if (strncmp(cmd, "LOG ", 4) == 0) {
        add_log(cmd + 4, 0, 5);
        uart_send_str("Log added\n");
    } else if (strncmp(cmd, "DELAY ", 6) == 0) {
        int delay;
        char msg[20];
        sscanf(cmd + 6, "%d %[^]", &delay, msg);
        add_log(msg, delay, 5);
        uart_send_str("Delayed task added\n");
    } else if (strncmp(cmd, "LIST", 4) == 0) {
        print_log();
    } else if (strncmp(cmd, "SAVE", 4) == 0) {
        save_logs_to_eeprom();
    } else if (strncmp(cmd, "LOAD", 4) == 0) {
        load_logs_from_eeprom();
    } else {
        uart_send_str("Unknown command\n");
    }
}

// ========== MAIN ==========
int main(void) {
    uart_init();
    timer_init();
    DDRB |= (1 << PB0); // LED pin

    uart_send_str("System Ready\n");

    while (1) {
        if (!buffer_is_empty()) {
            char c = buffer_read();
            uart_send(c); // echo
            if (c == '\n' || c == '\r') {
                cmd_buf[idx] = '\0';
                if (idx > 0) parse_command(cmd_buf);
                idx = 0;
            } else if (idx < MAX_CMD_LEN - 1) {
                cmd_buf[idx++] = c;
            }
        }

        if (tick) {
            tick = 0;
            execute_tasks();
        }
    }
}
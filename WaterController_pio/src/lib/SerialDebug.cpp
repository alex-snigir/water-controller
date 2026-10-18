// SerialDebug
// 
// v0.1 20221214 - Initial
// v0.2 20221202 - refactoring serial reading
// v0.3 20231206 - Initialize CMD_BUF
// v0.4 20260402 - Add default command handler for empty command
// v0.5 20260513 - Adding backspace or delete keys handling
// v0.6 20260606 - Refactoring, AI suggested improvements, etc...
// v0.7 20260614 - Minor fixes

#include "Application.h"
#include "SerialDebug.h"
#include "Log.h"
#include <string.h>

#define CMD_HELP "help"
#define CMD_INFO "info"

SerialDebug serialDebug;

SerialDebug::SerialDebug() {
    memset(CMD_BUF, 0, CMD_BUFFER_SIZE);
}

void SerialDebug::addInfoCommandHandler(void (*handler)()) {
    if (info_cmds == 0){
        info_cmds = new INFO_CMD_TYPE(handler);
    }
    else {
        INFO_CMD_TYPE *p = info_cmds;
        while(p->next != 0) {
            p = p->next;
        }
        p->next = new INFO_CMD_TYPE(handler);
    }
}

void SerialDebug::addCommandHandler(const char*cmd, void (*handler)(void)) {
    if (cmds == 0){
        cmds = new CMD_TYPE(cmd, handler);
    }
    else {
        CMD_TYPE *p = cmds;
        while(p->next != 0) {
            p = p->next;
        }
        p->next = new CMD_TYPE(cmd, handler);
    }
}

void SerialDebug::handleClient() {

    while (true) {
        if (Serial.available() > 0) {
            int byte = Serial.read();
            if (byte == '\n') { // Don't check '\r', it will print help twice
                // cmd_idx=0 - Will print help and run default action
                CMD_BUF[cmd_idx] = 0;
                cmd_idx = 0;
                handle_cmd();
            }
            else {
                if (byte == 8 || byte == 127) {
                    if (cmd_idx > 0) {
                        cmd_idx--;
                        Serial.print("\b \b");
                    }
                }
                else if (cmd_idx < CMD_BUFFER_SIZE-1) {
                    if (byte > 32 && byte < 128) {
                        CMD_BUF[cmd_idx] = (char)byte;
                        cmd_idx++;
                    }
                }
            }
        }
        else {
            break;
        }
    }
}

void SerialDebug::handle_cmd() {
    Serial.printf("\nCMD: '%s' (%d)\n", CMD_BUF, strlen(CMD_BUF));
    if (strlen(CMD_BUF) == 0) {
        run_help_cmd();
        run_cmd(CMD_BUF); // This will run the command with an empty string, which can be used for a default action (e.g., show status)
    }
    else if (strcmp(CMD_BUF, CMD_HELP) == 0) {
        run_help_cmd();
    }
    else if (strcmp(CMD_BUF, CMD_INFO) == 0) {
        run_info_cmd();
    } else {
        run_cmd(CMD_BUF);
    }
}

void SerialDebug::run_help_cmd() {
    Serial.println("SerialDebug commands:\n=====================");
    Serial.println("  info - get info");
    CMD_TYPE *p = cmds;
    while(p != 0) {
        Serial.printf("  %s\n", p->cmd);
        p = p->next;
    }
}

void SerialDebug::run_info_cmd() {
    INFO_CMD_TYPE *p = info_cmds;
    while(p != 0) {
        p->handler();
        p = p->next;
    }
}

void SerialDebug::run_cmd(const char* cmd) {
    CMD_TYPE *p = cmds;
    while(p != 0) {
        if (strcmp(p->cmd, cmd) == 0) {
            p->handler();
            return;
        }
        p = p->next;
    }
}

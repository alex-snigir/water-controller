#pragma once

#define CMD_BUFFER_SIZE  (30)

struct INFO_CMD_TYPE {
    void (*handler)(void);
    INFO_CMD_TYPE *next = 0;
    INFO_CMD_TYPE(void (*_handler)(void)) {
        handler = _handler;
        next = 0;
    }
};

struct CMD_TYPE {
    void (*handler)(void);
    CMD_TYPE *next = 0;
    const char* cmd;
    CMD_TYPE(const char* _cmd, void (*_handler)(void)) {
        cmd = _cmd;
        handler = _handler;
        next = 0;
    }
};

class SerialDebug {

    public:
        SerialDebug();

        void begin();
        void addInfoCommandHandler(void (*handler)(void));
        void addCommandHandler(const char*cmd, void (*handler)(void));

        void handleClient();

    private:
        int cmd_idx = 0;
        char CMD_BUF[CMD_BUFFER_SIZE];

        void handle_cmd();

        CMD_TYPE *cmds = 0;
        INFO_CMD_TYPE *info_cmds = 0;
        
        void run_info_cmd();
        void run_help_cmd();
        void run_cmd(const char*);
};

extern SerialDebug serialDebug;

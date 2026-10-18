#ifndef _SN_PROFILER_CLIENT_H_
#define _SN_PROFILER_CLIENT_H_

class Profiler {
    public:
        Profiler();
        void begin(unsigned long test_interval, unsigned long threshold1, unsigned long threshold2, unsigned long threshold3, unsigned long threshold_max);
  
        void dump_info(void);

        void beginLoop();
        void endLoop();
        // void handle();

    private:
        unsigned long test_interval;
        unsigned long test_interval_begin;
        unsigned long loop_cnt;

        unsigned long threshold1;
        unsigned long threshold2;
        unsigned long threshold3;
        unsigned long threshold_max;

        unsigned long between_loops_max;

        unsigned long threshold1_cnt;
        unsigned long threshold2_cnt;
        unsigned long threshold3_cnt;
        unsigned long threshold4_cnt;

        unsigned long threshold1_max;
        unsigned long threshold2_max;
        unsigned long threshold3_max;
        unsigned long threshold4_max;

        unsigned long timestamp;

        void init();
    
};

extern Profiler profiler;

#endif

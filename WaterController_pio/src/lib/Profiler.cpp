// Profiler
//
// Versions:
//   v0.1 20251026

#include "Application.h"
#include "Profiler.h"
#include "Log.h"

Profiler profiler; // Created Profiler instance

Profiler::Profiler()
{
}

void Profiler::begin(unsigned long test_interval, unsigned long threshold1, unsigned long threshold2, unsigned long threshold3, unsigned long threshold_max)
{
    this->test_interval = test_interval;
    this->threshold1 = threshold1;
    this->threshold2 = threshold2;
    this->threshold3 = threshold3;
    this->threshold_max = threshold_max;

    Serial.println("==[Profiler Config]===============");
    Serial.printf("threshold 1: %d mills\n", test_interval);
    Serial.printf("threshold 1: %d micros\n", threshold1);
    Serial.printf("threshold 2: %d micros\n", threshold2);
    Serial.printf("threshold 3: %d micros\n", threshold3);
    Serial.printf("threshold max: %d micros\n", threshold_max);
    Serial.println("==============================");
    test_interval_begin = 0L;
    timestamp = 0L;
}


void Profiler::init() {
    loop_cnt = 0L;
    threshold1_cnt = 0L;
    threshold2_cnt = 0L;
    threshold3_cnt = 0L;
    threshold4_cnt = 0L;
    threshold1_max = 0L;
    threshold2_max = 0L;
    threshold3_max = 0L;
    threshold4_max = 0L;
    between_loops_max = 0L;
}

void Profiler::beginLoop() {
    unsigned long delta = micros() - timestamp;
    if (timestamp != 0L) {
        between_loops_max = delta > between_loops_max ? delta : between_loops_max;
    }   
    timestamp = micros();
    if (test_interval_begin == 0L) {
        test_interval_begin = millis();
    }
}

void Profiler::endLoop() {
    unsigned long delta = micros() - timestamp;
    loop_cnt++;
    if (delta < threshold1) {
        threshold1_cnt++;
        threshold1_max = delta > threshold1_max ? delta : threshold1_max;
    } else {
        if (delta > threshold3) {
            threshold4_cnt++;
            threshold4_max = delta > threshold4_max ? delta : threshold4_max;
            if (delta > threshold_max) {
                    Serial.print("#####==>[Profiler THRESHOLD MAX]==>>>> ");
                    Serial.print(delta);
                    Serial.println(" millis !!!");
            }
        } else {
            if (delta < threshold2) {
                threshold2_cnt++;
                threshold2_max = delta > threshold2_max ? delta : threshold2_max;
            }
            else {
                threshold3_cnt++;
                threshold3_max = delta > threshold3_max ? delta : threshold3_max;
            }
        }
    }
    unsigned long t = millis();
    if (t - test_interval_begin >= test_interval) {
        dump_info();
        test_interval_begin = 0L;
    }
    timestamp = micros();
}

void Profiler::dump_info(void) {
    unsigned long dump_timestamp = micros();
    Serial.println("==[Profiler Dump]===============");
    Serial.print("loop cnt: ");
    Serial.print(loop_cnt);
    Serial.print("\nthreshold 1 cnt: ");
    Serial.print(threshold1_cnt);
    Serial.print("\nthreshold 2 cnt: ");
    Serial.print(threshold2_cnt);
    Serial.print("\nthreshold 3 cnt: ");
    Serial.print(threshold3_cnt);
    Serial.print("\nthreshold 4 cnt: ");
    Serial.print(threshold4_cnt);
    Serial.print("\nthreshold max: ");
    Serial.print(threshold1_max);
    Serial.print("  ");
    Serial.print(threshold2_max);
    Serial.print(" ");
    Serial.print(threshold3_max);
    Serial.print(" ");
    Serial.print(threshold4_max);
    Serial.print(" beetween: ");
    Serial.print(between_loops_max);
    Serial.print(" micros \ndump spent: ");
    Serial.print(micros() - dump_timestamp);
    Serial.println(" micros\n==============================");
    init();
}

// void handle();

#include <assert.h>
#include <stdio.h>
#include "retry.h"
int main(void){
 RetryState state={0};uint64_t now=1000;
 retry_update(&state,true,false,now);assert(state.due==6000);
 assert(!retry_take(&state,5999));
 for(unsigned i=0;i<10000;i++){
  now=state.due;assert(retry_take(&state,now));retry_failure(&state,now);
  assert(state.due==now+5000);
 }
 assert(state.attempts==10000);
 retry_pause(&state);retry_update(&state,true,false,now+5000);
 assert(state.paused && !state.due && !retry_take(&state,now+100000));
 retry_reset(&state);retry_update(&state,true,false,now);
 assert(state.due);retry_update(&state,false,false,now);assert(!state.due);
 retry_update(&state,true,false,now);assert(state.due);
 state.blocked=true;retry_update(&state,true,false,now);retry_failure(&state,now);assert(!state.due);
 retry_reset(&state);retry_failure(&state,now);
 for(unsigned i=0;i<6;i++){now=state.due;assert(retry_take(&state,now));retry_failure(&state,now);}
 assert(state.attempts==6 && !state.due);
 retry_reset(&state);retry_update(&state,true,true,now);assert(!state.due);
 retry_update(&state,true,false,now);assert(state.due==now+5000);
 puts("PASS: 10000 delayed-start attempts, five-second pacing, Stop pause, autostart off, safety block, bounded manual recovery, disconnect retry");
}

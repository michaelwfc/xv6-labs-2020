#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

/**
A barrier has exactly three responsibilities:
1. Protect shared state from races
2. Put threads to sleep until a condition becomes true
3. Distinguish one barrier round from the next
Every field in struct barrier exists to satisfy one of those responsibilities. Remove or misuse any one of them, and the barrier breaks in subtle, time-travel-like ways.

`pthread_mutex_t barrier_mutex`
Every read or write of barrier state must happen while holding this mutex.


*/
struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.

  // add lock, same as   xv6 acquire(&lock)
  pthread_mutex_lock(&bstate.barrier_mutex);
  // rember the round I enter into the barrier
  int myround = bstate.round;
  bstate.nthread++;
  
  // printf("run barrier in nthread %ld at round %d", bstate.nthread, bstate.round);

  if(bstate.nthread==nthread){
    // the last thread in this round
    bstate.nthread = 0;
    bstate.round++;
    if(bstate.round % 1000==0){
      printf("start round %d\n", bstate.round);
    }
    // same as wakeup(chan), wake up all the threads sleeping on the barrier_cond
    pthread_cond_broadcast(&bstate.barrier_cond);
  }
  else{
    // same as while(condition not ture) sleep(chan, lock)
    // while(bstate.nthread < nthread){

    // wait until the round changes, instead wait for thread changes
    while(myround==bstate.round){
      pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
    }
}
  pthread_mutex_unlock(&bstate.barrier_mutex);
  return; 
}


static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    // printf("run thread %ld at round %d", n,t);
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  printf("start testing with nthread = %d\n", nthread);

  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}

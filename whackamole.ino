#include <avr/pgmspace.h>

// Game configuration
// TODO: consider making sets of these to make it more difficult.
const uint32_t delay_time = 1000;  // in ms.
const uint8_t mole_pop_prob = 80;  // 80% of the time, a mole will pop after delay_time.
const uint32_t mole_expire_delay = 500;  // in ms

// Mole config
// Each moles has a button number and an led number.
struct mole_config {
  uint8_t button;
  uint8_t led;
};
// This is designed for the Simon PCB.
//   LED pins: 3, 5, 10, 13
//   Button pins: 2, 6, 9, 12
//   Buzzer pins: 4, 7
const struct mole_config moles[] PROGMEM = {
  { 2, 3 },
  { 6, 5 },
  { 9, 10 },
  { 12, 13 }
};
const uint8_t num_moles = sizeof(moles) / sizeof(*moles);


// Mole state
//
struct mole_state {
  // This will be 0 if the mole is not lighted, and will be the "mole number"
  // of the expire event if it is lighted.
  uint16_t mole_number;
};
// Make sure this has num_moles elements.
struct mole_state mole_state[] = {
  { 0 }, { 0 }, { 0 }, { 0 }
};
uint16_t next_mole_number = 1;


// Program State
//
// The program can be in a bunch of states.
// These determine the first-order behavior of the app.
enum program_state {
  // The program is not running. Perhaps run an "attract mode."
  state_stopped,

  // Moles are ready to be whacked.
  state_running,

  // We won!
  state_won,

  // Uh oh, we lost.
  state_lost
};
const uint8_t num_program_states = state_lost + 1;

enum program_state state = state_stopped;


// Event Queue
//
// The program is driven by an event queue.
// Each event has a "fire time" which is the minimum number of millis (according to millis())
// at which the event can happen.
enum event_type {
  event_mole_can_pop,  // A mole may pop up at this time.


  event_mole_expired   // A mole was up so long that you lost.
};
const uint8_t num_event_types = event_mole_expired + 1;

struct event {
  uint32_t event_millis;
  enum event_type etype;
  uint16_t event_mole_number;
};

// Constructors for the various events
inline struct event makeMoleCanPopEvent() {
  return { millis() + delay_time, event_mole_can_pop };
}

inline struct event makeMoleExpiresEvent() {
  // TODO: add some waggle to the expire time.
  return { millis() + mole_expire_delay, event_mole_expired, next_mole_number };
}

// Heap operations on the event queue.
const uint8_t MAX_HEAP_SIZE = 20;
struct event event_heap[MAX_HEAP_SIZE];
uint8_t event_heap_size = 0;

// For a node at index i in the heap array, return the index of its parent.
inline uint8_t heap_parent(uint8_t i) {
  return (i - 1) / 2;
}

// For a node at index i in the heap array, return the index of its left child.
inline uint8_t heap_left(uint8_t i) {
  return 2 * i + 1;
}

// For a node at index i in the heap array, return the index of its right child.
inline uint8_t heap_right(uint8_t i) {
  return 2 * i + 2;
}

// Remove all of the events from the heap.
inline void heap_clear() {
  event_heap_size = 0;
}

// Insert an event into the heap.
void heap_insert(struct event ev) {
  event_heap_size++;
  uint8_t i = event_heap_size;
  uint8_t parent = heap_parent(i);
  while (i > 0 && ev.event_millis < event_heap[parent].event_millis) {
    event_heap[i] = event_heap[parent];
    i = parent;
  }
  event_heap[i] = ev;
}

// Given a heap where the node at index i may not obey the heap property,
// restore the heap property.
void heapify(uint8_t i) {
  uint8_t l = heap_left(i);
  uint8_t r = heap_right(i);
  uint8_t largest;
  if (l < event_heap_size && event_heap[l].event_millis < event_heap[i].event_millis) {
    largest = l;
  } else {
    largest = i;
  }

  if (r < event_heap_size && event_heap[r].event_millis < event_heap[largest].event_millis) {
    largest = r;
  }

  if (largest != i) {
    struct event tmp = event_heap[largest];
    event_heap[largest] = event_heap[i];
    event_heap[i] = tmp;

    heapify(largest);
  }
}

// Remove and return the event in the heap with the earliest event_millis.
// NOTE: assumes the heap is not empty. The caller MUST check.
struct event heap_extract_min() {
  struct event imin = event_heap[0];
  event_heap[0] = event_heap[event_heap_size - 1];
  event_heap_size--;
  heapify(0);
  return imin;
}

//////////////////////

void startANewGame() {
  // Clear out old events and start the game by popping a mole.
  heap_clear();
  heap_insert(makeMoleCanPopEvent());

  // Clear out any previous moles.
  for (int i = 0; i < num_moles; ++i) {
    mole_state[i].mole_number = 0;
    digitalWrite(moles[i].led, LOW);
  }

  state = state_running;
}

void maybeStartANewGame() {
  // Wait for a button press, then start the game.
  if (anyButtonPressed()) startANewGame();
}

bool anyButtonPressed() {
  for (int i = 0; i < num_moles; ++i) {
    if (digitalRead(moles[i].button)) {
      return true;
    }
  }
  return false;
}

void handleMoleCanPop() {
  if (random(100) >= mole_pop_prob) {
    return;
  }

  // TODO: make sure we're not selecting an already active mole.
  int mole = random(num_moles);

  // 1) Set the mole state.
  mole_state[mole].mole_number = next_mole_number;
  digitalWrite(moles[mole].led, HIGH);

  // 2) Create the mole expires event.
  heap_insert(makeMoleExpiresEvent());

  // 3) Create the next can pop event.
  heap_insert(makeMoleCanPopEvent());

  next_mole_number++;
}

void handleMoleExpired() {
  state = state_lost;
}

// Handle program states.
void program_stopped() {
  // TODO: add an attraction animation. For now, just turn them all on.
  for (int i = 0; i < num_moles; i++) {
    digitalWrite(moles[i].led, HIGH);
  }

  maybeStartANewGame();
}

void program_running() {
  // Start by looking for any depressed buttons, removing necessary moles from game state.
  for (int i = 0; i < num_moles; ++i) {
    if (digitalRead(moles[i].button) == HIGH) {
      if (mole_state[i].mole_number != 0) {
        mole_state[i].mole_number = 0;
        digitalWrite(moles[i].led, LOW);
      }
    }
  }

  // Then, fire the next event from the queue.
  uint32_t curr_ms = millis();
  if (curr_ms > event_heap->event_millis) {
    struct event ev = heap_extract_min();
    switch (ev.etype) {
    case event_mole_can_pop:
      handleMoleCanPop();
      break;

    case event_mole_expired:
      handleMoleExpired();

    case num_event_types:
    default:
      // Ignore probable error.
      break;
    }
    // TODO: add a delay here until the next event.
  }
}

void program_won() {
  // TODO: add a happy song!
  maybeStartANewGame();
}

void program_lost() {
  // TODO: add a sad song!
  maybeStartANewGame();
}

void loop() {
  switch (state) {
  case state_stopped: program_stopped();
  case state_running: program_running();
  case state_won: program_won();
  case state_lost: program_lost();

  default:
    // Ignore this probable error.
    break;
  }
}

void setup() {
  // Configure the LEDs and buttons for the moles.
  for (int i = 0; i < num_moles; ++i) {
    pinMode(moles[i].button, INPUT_PULLUP);
    pinMode(moles[i].led, OUTPUT);
  }

  // The event queue and mole state will be inited when the game starts.
}

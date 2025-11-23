/*******************************************************************************
 * ISAAC.H - Cryptographic Pseudorandom Number Generator
 *******************************************************************************
 * 
 * EDUCATIONAL PURPOSE:
 * This file demonstrates fundamental concepts in:
 *   - Cryptographic pseudorandom number generation (CSPRNG)
 *   - Stream cipher design and implementation
 *   - Nonlinear mixing functions for avalanche effect
 *   - Indirection-based data transformations
 *   - State space analysis and period calculation
 *   - Deterministic chaos and unpredictability
 * 
 * CORE CONCEPT - ISAAC CIPHER:
 * ISAAC (Indirection, Shift, Accumulate, Add, and Count) is a fast 
 * cryptographic random number generator designed by Bob Jenkins in 1996.
 * 
 * KEY PROPERTIES:
 *   1. Cryptographic strength: Suitable for encryption, not just statistics
 *   2. Speed: Generates random values very quickly (competitive with RC4)
 *   3. Large state: 8KB internal state (256 * 32-bit words)
 *   4. Long period: Average cycle length ≥ 2^8295 (astronomically large)
 *   5. Unbiased output: Each 32-bit value equally likely
 *   6. No known attacks: Unbroken since 1996
 * 
 * CRYPTOGRAPHIC VS STATISTICAL RANDOMNESS:
 * ┌────────────────────┬──────────────────┬──────────────────────────────┐
 * │    Generator       │      Type        │         Best Use             │
 * ├────────────────────┼──────────────────┼──────────────────────────────┤
 * │ Linear Congruential│  Statistical     │ Games, simulations           │
 * │ Mersenne Twister   │  Statistical     │ Monte Carlo, science         │
 * │ ISAAC              │  Cryptographic   │ Encryption, security         │
 * │ AES-CTR            │  Cryptographic   │ High-security applications   │
 * └────────────────────┴──────────────────┴──────────────────────────────┘
 * 
 * CRYPTOGRAPHIC STRENGTH EXPLAINED:
 * 
 * A cryptographically secure PRNG must satisfy:
 *   1. Unpredictability: Cannot predict future values from past values
 *   2. Backtracking resistance: Cannot reconstruct previous values
 *   3. Indistinguishability: Output looks like true random data
 * 
 * ISAAC achieves this through:
 *   - Large state space (2^8288 possible states)
 *   - Nonlinear transformations (indirection, XOR, bit shifts)
 *   - Data-dependent operations (array lookups based on state)
 * 
 * MEMORY LAYOUT:
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │ ISAACCipher Structure (4104 bytes = ~4KB)                               │
 * ├─────────────────┬───────────────────────────────────────────────────────┤
 * │ u32 count       │ Position in result array (0 to 255)                   │
 * │ (4 bytes)       │ Decrements on each call to isaac_get_next()           │
 * ├─────────────────┼───────────────────────────────────────────────────────┤
 * │ u32 rsl[256]    │ Result array: 256 precomputed random values           │
 * │ (1024 bytes)    │ Generated in batches by isaac_shuffle()               │
 * ├─────────────────┼───────────────────────────────────────────────────────┤
 * │ u32 mem[256]    │ Internal state memory: mixed by shuffle algorithm     │
 * │ (1024 bytes)    │ Core of the cipher's entropy pool                     │
 * ├─────────────────┼───────────────────────────────────────────────────────┤
 * │ u32 a           │ Accumulator: mixed with state on each step            │
 * │ (4 bytes)       │                                                       │
 * ├─────────────────┼───────────────────────────────────────────────────────┤
 * │ u32 b           │ Last result: fed back into algorithm                  │
 * │ (4 bytes)       │                                                       │
 * ├─────────────────┼───────────────────────────────────────────────────────┤
 * │ u32 c           │ Counter: incremented every shuffle (prevents cycles)  │
 * │ (4 bytes)       │                                                       │
 * ├─────────────────┼───────────────────────────────────────────────────────┤
 * │ u32 initialized │ Flag: 1 if isaac_init() completed, 0 otherwise        │
 * │ (4 bytes)       │                                                       │
 * └─────────────────┴───────────────────────────────────────────────────────┘
 * 
 * INTERNAL STATE VISUALIZATION:
 * 
 *   rsl[] (results):          mem[] (state):
 *   ┌──────┐                   ┌──────┐
 *   │  R0  │  ← current        │  M0  │
 *   │  R1  │                   │  M1  │
 *   │  R2  │                   │  M2  │
 *   │  R3  │                   │  M3  │
 *   │  ..  │                   │  ..  │
 *   │ R254 │                   │ M254 │
 *   │ R255 │                   │ M255 │
 *   └──────┘                   └──────┘
 *       ↑                         ↑
 *   count (decrements)     Mixed by shuffle
 * 
 * USAGE PATTERN:
 * 1. Seed:        u32 seed[4] = {0x1234, 0x5678, 0xABCD, 0xEF01};
 *                 ISAACCipher cipher;
 *                 isaac_init(&cipher, seed, 4);
 * 
 * 2. Generate:    u32 random1 = isaac_get_next(&cipher);  // Returns R255
 *                 u32 random2 = isaac_get_next(&cipher);  // Returns R254
 *                 ...
 *                 u32 random256 = isaac_get_next(&cipher); // Returns R0
 * 
 * 3. Auto-reseed: u32 random257 = isaac_get_next(&cipher); // Calls isaac_shuffle()
 *                                                          // Generates new batch
 * 
 * ALGORITHM OVERVIEW:
 * 
 * Initialization (isaac_init):
 *   1. Initialize a, b, c to 0
 *   2. Create mixing state (a-h) from golden ratio
 *   3. Mix seed data through state variables 4 times
 *   4. Initialize mem[] array with scrambled seed
 *   5. Perform second pass for thorough diffusion
 *   6. Run first shuffle to generate initial results
 * 
 * Generation (isaac_shuffle):
 *   For each of 256 state words:
 *     1. Mix accumulator 'a' using shift operations
 *     2. Add state from opposite half of array
 *     3. Compute new state using indirect lookup
 *     4. Generate result using second indirect lookup
 *     5. Feed result back into state
 * 
 * SECURITY GUARANTEES:
 * 
 * Period: Expected to be at least 2^8295 before repeating
 *   - For comparison: 2^128 would take billions of years to exhaust
 *   - ISAAC's period is incomprehensibly larger
 * 
 * Cryptanalysis Results (as of 2025):
 *   - No practical attacks known
 *   - Theoretical distinguisher exists but requires 2^4.67×α operations
 *   - Still considered secure for stream cipher applications
 * 
 * COMPLEXITY ANALYSIS:
 *   - Space: O(1) - Fixed 4KB state
 *   - Init:  O(1) - Fixed 256 iterations
 *   - Next:  O(1) - Array lookup (amortized: shuffle every 256 calls)
 * 
 ******************************************************************************/

#ifndef ISAAC_H
#define ISAAC_H

#include "types.h"

/*******************************************************************************
 * CONSTANTS
 ******************************************************************************/

/*
 * ISAAC_SIZE - Number of 32-bit words in state arrays
 * 
 * VALUE: 256 (2^8)
 * 
 * JUSTIFICATION:
 *   - Large enough for cryptographic security (256 * 32 = 8192 bits state)
 *   - Power of 2 for efficient modulo operations using bitwise AND
 *   - Balances memory usage vs. security (8KB total state)
 * 
 * STATE SPACE CALCULATION:
 *   Total internal state: 256 words * 2 arrays + 3 registers
 *                      = approximately 512 words * 32 bits = 16384 bits
 *   Possible states:    2^16384 (incomprehensibly large)
 */
#define ISAAC_SIZE 256

/*******************************************************************************
 * ISAACCIPHER - Cryptographic PRNG State
 *******************************************************************************
 * 
 * FIELDS:
 *   count:       Current position in rsl[] (decrements from 255 to 0)
 *                When reaches 0, triggers new shuffle
 * 
 *   rsl:         Results array - 256 precomputed random values
 *                Generated in batches by isaac_shuffle()
 *                Returned one at a time by isaac_get_next()
 * 
 *   mem:         Memory/state array - internal entropy pool
 *                Continuously mixed by shuffle algorithm
 *                Source of unpredictability
 * 
 *   a:           Accumulator - mixed with different shift amounts
 *                Provides nonlinearity in state updates
 * 
 *   b:           Last result - fed back into computation
 *                Creates dependency between consecutive values
 * 
 *   c:           Counter - incremented on each shuffle
 *                Prevents state cycles and guarantees long period
 * 
 *   initialized: Initialization flag
 *                0 = not initialized (do not use)
 *                1 = ready for use
 * 
 * INVARIANTS:
 *   - 0 <= count <= ISAAC_SIZE
 *   - initialized == 1 before first use
 *   - All arrays fully populated after init
 * 
 * MEMORY:
 *   Total size: 4104 bytes (4KB)
 *   Stack-friendly: Can be allocated on stack or heap
 * 
 ******************************************************************************/
typedef struct {
    u32 count;              /* Position in result array [0, 256] */
    u32 rsl[ISAAC_SIZE];    /* Result array: random output values */
    u32 mem[ISAAC_SIZE];    /* State array: internal entropy pool */
    u32 a;                  /* Accumulator: nonlinear mixing */
    u32 b;                  /* Last result: feedback */
    u32 c;                  /* Counter: cycle prevention */
    u32 initialized;        /* Initialization flag: 1=ready, 0=not ready */
} ISAACCipher;

/*******************************************************************************
 * PUBLIC API
 ******************************************************************************/

/*
 * isaac_init - Initialize ISAAC cipher with seed
 * 
 * @param cipher    Pointer to ISAACCipher structure to initialize
 * @param seed      Array of 32-bit seed values (or NULL for zero seed)
 * @param seed_len  Number of seed values (0 to ISAAC_SIZE)
 * 
 * ALGORITHM OVERVIEW:
 *   1. Zero-initialize state variables (a, b, c)
 *   2. Create mixing state from golden ratio φ = (1+√5)/2
 *   3. Scramble mixing state 4 times for diffusion
 *   4. Mix seed into mem[] array (two passes for thorough mixing)
 *   5. Generate initial batch of random values
 * 
 * SEED HANDLING:
 *   - If seed is NULL, uses all zeros (still secure due to mixing)
 *   - If seed_len < 256, remaining slots filled with zeros
 *   - If seed_len > 256, only first 256 values used
 * 
 * GOLDEN RATIO USAGE:
 *   φ = 1.618033988749... 
 *   0x9e3779b9 = floor(2^32 / φ)
 *   
 *   Properties:
 *     - Irrational number (never repeats)
 *     - Uniform distribution modulo 2^32
 *     - Used in many hash functions (e.g., Knuth's multiplicative hash)
 * 
 * MIXING PROCESS:
 *   Each mixing round performs 8 operations:
 *     a ^= b << 11;  d += a;  b += c;   (shift, mix, accumulate)
 *     b ^= c >> 2;   e += b;  c += d;   (shift, mix, accumulate)
 *     c ^= d << 8;   f += c;  d += e;   (shift, mix, accumulate)
 *     d ^= e >> 16;  g += d;  e += f;   (shift, mix, accumulate)
 *     e ^= f << 10;  h += e;  f += g;   (shift, mix, accumulate)
 *     f ^= g >> 4;   a += f;  g += h;   (shift, mix, accumulate)
 *     g ^= h << 8;   b += g;  h += a;   (shift, mix, accumulate)
 *     h ^= a >> 9;   c += h;  a += b;   (shift, mix, accumulate)
 * 
 *   After 4 rounds, each bit in (a-h) depends on every seed bit
 *   This is called "avalanche" - small input changes cause large output changes
 * 
 * SIDE EFFECTS:
 *   - Fills cipher->mem[] with scrambled seed data
 *   - Fills cipher->rsl[] with initial random values
 *   - Sets cipher->initialized = 1
 *   - Sets cipher->count = ISAAC_SIZE
 * 
 * COMPLEXITY: O(1) time - exactly 768 iterations (256*3)
 */
void isaac_init(ISAACCipher* cipher, const u32* seed, u32 seed_len);

/*
 * isaac_next - Generate new batch of 256 random values
 * 
 * @param cipher  Pointer to initialized ISAAC cipher
 * 
 * ALGORITHM:
 *   Calls isaac_shuffle() to regenerate rsl[] array
 *   Resets count to 0 (ready for new batch)
 * 
 * WHEN CALLED:
 *   - Automatically by isaac_get_next() when count reaches 0
 *   - Can be called manually to force regeneration
 * 
 * SIDE EFFECTS:
 *   - Modifies cipher->rsl[] (new random values)
 *   - Modifies cipher->mem[] (state evolution)
 *   - Increments cipher->c (cycle counter)
 *   - Updates cipher->a and cipher->b
 *   - Resets cipher->count = 0
 * 
 * COMPLEXITY: O(1) time - exactly 256 iterations
 */
void isaac_next(ISAACCipher* cipher);

/*
 * isaac_get_next - Get next random 32-bit value
 * 
 * @param cipher  Pointer to initialized ISAAC cipher
 * @return        Pseudorandom 32-bit value (uniformly distributed)
 * 
 * ALGORITHM:
 *   1. If count == 0, call isaac_next() to generate new batch
 *   2. Decrement count
 *   3. Return rsl[count]
 * 
 * RETURN VALUE PROPERTIES:
 *   - Uniformly distributed: Each value [0, 2^32-1] equally likely
 *   - Statistically independent: No correlation between consecutive values
 *   - Cryptographically unpredictable: Cannot predict future values
 * 
 * USAGE EXAMPLES:
 *   
 *   Generate random byte (0-255):
 *     u8 random_byte = isaac_get_next(&cipher) & 0xFF;
 *   
 *   Generate random 16-bit value (0-65535):
 *     u16 random_short = isaac_get_next(&cipher) & 0xFFFF;
 *   
 *   Generate random float [0.0, 1.0):
 *     double random_float = isaac_get_next(&cipher) / (double)0xFFFFFFFF;
 *   
 *   Generate random integer in range [min, max]:
 *     u32 range = max - min + 1;
 *     u32 random_int = min + (isaac_get_next(&cipher) % range);
 *     Note: Modulo bias exists if range doesn't divide 2^32 evenly
 *   
 *   Generate random boolean:
 *     bool random_bool = isaac_get_next(&cipher) & 1;
 * 
 * PERFORMANCE:
 *   - Amortized O(1) time
 *   - Worst case O(1) time (when triggering shuffle)
 *   - Approximately 4 CPU cycles per value on modern x86-64
 * 
 * COMPLEXITY: O(1) amortized (shuffle every 256th call)
 */
u32 isaac_get_next(ISAACCipher* cipher);

/*******************************************************************************
 * SECURITY CONSIDERATIONS
 ******************************************************************************/

/*
 * SEED ENTROPY:
 *   - Use high-quality random seed from /dev/urandom or hardware RNG
 *   - Poor seed = predictable output (garbage in, garbage out)
 *   - Minimum recommended seed size: 16 bytes (128 bits)
 * 
 * CRYPTOGRAPHIC APPLICATIONS:
 *   - Stream cipher: XOR plaintext with isaac_get_next() output
 *   - Session keys: Generate unpredictable symmetric keys
 *   - Nonces: Create unique values for protocols
 *   - Packet obfuscation: Mask opcodes in RuneScape protocol
 * 
 * NON-CRYPTOGRAPHIC APPLICATIONS:
 *   - Procedural generation (games, simulations)
 *   - Monte Carlo simulations
 *   - Randomized algorithms (quicksort, hash tables)
 * 
 * THREAD SAFETY:
 *   - ISAACCipher is NOT thread-safe
 *   - Use one cipher per thread, or add mutex protection
 * 
 * STATE SECURITY:
 *   - Internal state should be kept secret for encryption
 *   - If attacker learns full state, all future values predictable
 *   - Consider memory wiping (memset) after use in high-security contexts
 */

#endif /* ISAAC_H */

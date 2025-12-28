# Zyrox LLVM Obfuscator

### llvm compile and link-time plugin for obfuscating native code

# Why

why not ¯\\\_(ツ)\_/¯

One of my biggest projects, where I learned a lot about LLVM internals, binary formats, assembly and obfuscation techniques.

I believe that learning through building is the best way to learn, thus I built this project to learn more about these topics.

# Research

I have wrote 4 blogs explaining the concepts behind [Zyrox](https://peterr.dev/blogs/zyrox):

-   [Part I: Building Zyrox: A Custom LLVM Obfuscator](https://peterr.dev/blogs/zyrox/llvm-obfuscator-part-one)
-   [Part II: Control Flow Flattening](https://peterr.dev/blogs/zyrox/llvm-obfuscator-part-two)
-   [Part III: Encrypted Jump Tables](https://peterr.dev/blogs/zyrox/llvm-obfuscator-part-three)
-   [Part IV: The Finale](https://peterr.dev/blogs/zyrox/llvm-obfuscator-part-four)

These parts go deeper than this readme, and definitely worth a read if you are interested in the topic.

# Building

install llvm:

```shell
sudo apt update
sudo apt install llvm-18 llvm-18-dev clang-18
```

clone and compile zyrox:

```shell
git clone --recurse-submodules https://github.com/PeterHackz/zyrox.git
cd zyrox
cmake -S . -B build -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build --parallel 4
```

# Usage

Quick Usage:
```shell
clang -O0 -flto=full -c main.c -o out/main.o
clang -flto=full -fuse-ld=lld -Wl,--load-pass-plugin=./build/libzyrox.so out/main.o -o out/main
```

Advanced: Integrating with a cmake project:
```cmake
set(ZYROX_PLUGIN "<your path here>/libzyrox.so" CACHE FILEPATH "Path to libzyrox.so plugin")
file(REAL_PATH "${ZYROX_PLUGIN}" ZYROX_PLUGIN_ABS)

target_link_options(your_target PRIVATE
        "-fuse-ld=/usr/bin/ld.lld"
        "-Wl,--load-pass-plugin=${ZYROX_PLUGIN_ABS}"
)

target_compile_options(your_target PRIVATE
        -flto=full
)
```

I'll make a repo soon as a template for using Zyrox in cmake.

# Contacts

I get this is a complex topic, and this project was mostly for educational purposes, as well as to serve BSD Brawl.
If you have any question, or just want to chat, feel free to reach out to me:

-   Discord: `@s.b`
-   Email: `mail@peterr.dev` or `me@peterr.dev`
-   [Discord Server](https://discord.peterr.dev)

any help, through pull requests or issues is appreciated!

# How it works

`ZyroxPlugin.cpp` registers the pass, then links `siphash` (more on this later) and call `StringEncryption` to encrypt
strings.

The reason we encrypt strings early is so that decryption logic gets obfuscated too later.

then it calls `ModuleUtils::ExpandCustomAnnotations`
and `QuickConfig::RegisterPasses` to parse all `__attribute__((annotate("...")))` expressions and run
QuickJs config (located in `ZyroxConfig.js`)

Every function is obfuscated by calling `Zyrox::RunOnFunction` located in `ZyroxCore.cpp`,
more documentation about this will be provided in the future.

# Extra Util

switches create jump tables and PHI nodes are annoying to deal with thus we use `FunctionUtils` and `BasicBlockUtils`
to flatten (into if statements) and demote these respectively.

# Passes

oh man, where do I start

-   [Basic Block Splitter](#basic-block-splitter)
-   [Control Flow Flattening](#control-flow-flattening)
-   [Indirect Branching](#indirect-branching)
-   [Simple Indirect Branching](#simple-indirect-branching)
-   [Mixed Boolean Arithmetic](#mixed-boolean-arithmetic)

all js-plugin args are in `index.d.ts` so will not be talked about in this documentation.

for annotations documentation, [click here](#zyrox-annotations)

# Basic Block Splitter

This pass splits and shuffles a basic block into smaller ones. suppose we have this:

```c++
int __test_fn(int x)
{
    if (x == 2) {
        printf("x is 2\n");
    } else {
        printf("x is not 2!, x is: %d\n", x);
    }
    return x + 4 * x - 2 / 4;
}
```

which gets compiled into:

```asm
define internal i32 @__test_fn(i32 noundef %0) #0 !zyrox !8 !obfuscated !11 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = icmp eq i32 %3, 2
  br i1 %4, label %5, label %7

5:                                                ; preds = %1
  %6 = call i32 (ptr, ...) @printf(ptr noundef @.str.1)
  br label %10

7:                                                ; preds = %1
  %8 = load i32, ptr %2, align 4
  %9 = call i32 (ptr, ...) @printf(ptr noundef @.str.2, i32 noundef %8)
  br label %10

10:                                               ; preds = %7, %5
  %11 = load i32, ptr %2, align 4
  %12 = load i32, ptr %2, align 4
  %13 = mul nsw i32 4, %12
  %14 = add nsw i32 %11, %13
  %15 = sub nsw i32 %14, 0
  ret i32 %15
}

```

when using Basic Block Splitter with this config:

```js
z.RegisterPass(ObfuscationType.BasicBlockSplitter, {
    PassIterations: 1,
    "BasicBlockSplitter.SplitBlockChance": 100,
    "BasicBlockSplitter.SplitBlockMinSize": 2,
    "BasicBlockSplitter.SplitBlockMaxSize": 5,
});
```

it becomes:

```asm
define internal i32 @__test_fn(i32 noundef %0) #0 !zyrox !8 !obfuscated !11 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = icmp eq i32 %3, 2
  br i1 %4, label %5, label %14

5:                                                ; preds = %1
  %6 = call i32 (ptr, ...) @printf(ptr noundef @.str.1)
  br label %7

7:                                                ; preds = %14, %5
  %8 = load i32, ptr %2, align 4
  %9 = load i32, ptr %2, align 4
  %10 = mul nsw i32 4, %9
  %11 = add nsw i32 %8, %10
  br label %12

12:                                               ; preds = %7
  %13 = sub nsw i32 %11, 0
  ret i32 %13

14:                                               ; preds = %1
  %15 = load i32, ptr %2, align 4
  %16 = call i32 (ptr, ...) @printf(ptr noundef @.str.2, i32 noundef %15)
  br label %7
}
```

now it won't be that much different for such small function but notice how it split a basic block?
this is helpful combined with other passes like [Control Flow Flattening](#control-flow-flattening)

# Control Flow Flattening

Oh, man this pass have the most features among all lol.
I will start by explaining how it works then it's config
suppose we have this code:

```c++
LABEL_A: bool b = x == 2;
         IF EQ: goto LABEL_B
         goto LABEL_C
LABEL_B  do_stuff()
LABEL_C  do_other_stuff()
         goto LABEL_A
```

each basic block (A, B and C) gets assigned a unique dispatcher state, example: (simplified)

```c++
states = {
    1: LABEL_A,
    2: LABEL_B,
    3: LABEL_C,
};
```

then we inject a dispatcher block that controls everything and the code becomes:

```c++
         int state = 0;
LABEL_D  goto LABEL_CA // dispatcher label jumps to first condition block, label condition A
LABEL_CA if state == 1: goto LABEL_A
         // if not 1, go to check if it is label B (fallback)
LABEL_CB if state == 2: goto LABEL_B
LABEL_CC if state == 3: goto LABEL_CC
         // unreachable
         goto LABEL_D
LABEL_A: bool b = x == 2;
         // IF EQ: goto LABEL_B
         // goto LABEL_C
         state = 2 if b else 3 // update state for the block we want and back to dispatcher
         goto LABEL_D
LABEL_B  do_stuff()
LABEL_C  do_other_stuff()
         state = 1
         goto LABEL_D
```

now this have some flaws that the obfuscator fix. as you see since we have a single dispatcher variable, it is easy
to deobfuscate this since we know where a block is going to after it sets state. easy to fix!

```js
z.RegisterPass(ObfuscationType.ControlFlowFlattening, {
    PassIterations: 1,
    "ControlFlowFlattening.UseFunctionResolverChance": 60,
    "ControlFlowFlattening.UseGlobalStateVariablesChance": 60,
    "ControlFlowFlattening.UseOpaqueTransformationChance": 40,
    "ControlFlowFlattening.UseGlobalVariableOpaquesChance": 80,
    "ControlFlowFlattening.UseSipHashedStateChance": 40,
    "ControlFlowFlattening.CloneSipHashChance": 80,
});
```

let's go over the options 1 by 1:

-   `UseFunctionResolverChance`: injects a function to check for state, so instead of doing
    `if (state == expected_state)`, it does `if (injected_resolver(state))`. example:
    ```c++
    bool __fastcall cff_resolve_state_check_3585(__int64 a1)
    {
        return a1 == 0x288A6154F8A5E3E2LL;
    }
    ```
-   `UseGlobalStateVariablesChance`: save the state value to be compared in a global variable:
    ```c++
    bool __fastcall cff_resolve_state_check_506(__int64 a1)
    {
        return a1 == qword_1B20D8;
    }
    ```
-   `UseOpaqueTransformationChance`: obfuscate the check into some transformation that will only yield true for a specific
    state:
    ```c++
    bool __fastcall cff_resolve_state_check_7901(__int64 a1)
    {
        return ((((a1 ^ 0xEA9E45BB6099BC6ELL) + qword_1C64D8) << qword_1A63F0)
                   | (((a1 ^ 0xEA9E45BB6099BC6ELL)
                       + qword_1C64D8) >> qword_1ACE98)) == qword_1B0B80;
    }
    ```
-   `UseGlobalVariableOpaquesChance`: use a global variable instead of number when doing `UseOpaqueTransformationChance`,
    as you noticed in the example above. (`qword_1C64D8`, `qword_1A63F0`, `qword_1ACE98`)
-   `UseSipHashedStateChance`: uses a tiny customized `siphash` function to check for the state. so `if (state == 23872)`
    becomes something like `if (siphash(state) == 3874872081)` making it harder to find what a block is jumping to.
    block would do `state = 23872` when the condition of where it goes is hashed. each siphash call uses random values
    to make it harder to emulate it.
-   `CloneSipHashChance`: clone and even try when possible to inline `siphash` function making more than a sibling for it,
    which makes hooking a single function not enough. it is very much preferred to use this as it only increase binary
    size and does not affect performance.

# Indirect Branching

suppose we have this code:

```c++
if (x == 2) goto LABEL_A
            goto LABEL_B
LABEL_A:    do_stuff()
LABEL_B:    // ...
```

it would transform into:

```c++
@global jump_table = {0, &LABEL_A, &LABEL_B};
if (x == 2) goto jump_table[0] + @inline(decrypt(jump_table[1]));
            goto jump_table[0] + @inline(decrypt(jump_table[2]));
// ...
```

when this pass is used the plugin will output a `zyrox_tables.txt` file to be used by `PyPlugin.py`.

`PyPlugin.py` will encrypt the jump tables and patch relocation entries, then make the relocator point to jump_table[0]
of every table. a relocator basically does this:
`target.writePointer(base.add(value))`, so by setting value to 1, we make relocator give us base address and put it
in jump table at runtime, and we use it a long with the `goto` to generate the runtime address. on arm32 thumb mode
the pass automatically adds `| 1` after decryption.

to use `PyPlugin.py` simply do the following:

```shell
python3 PyPlugin.py --in yourfiletoencrypt.so --android
```

passing `--android` is important if you are targeting the arm64 version as the x86_64 version have a different relocator
signature.

you can also pass `--out` (by default it will use same file passed to `--in`) and you can pass `--tables` (by default it
is `zyrox_tables.txt`)

# Simple Indirect Branching

while indirect branching seems great it also comes with a performance hit as it is decrypting pointers at runtime,
this is a simple version that does not affect performance, where this:

```c++
if (x == 2) goto LABEL_A
            goto LABEL_B
LABEL_A:    do_stuff()
LABEL_B:    // ...
```

becomes:

```c++
            @stack jump_table = {&LABEL_B, &LABEL_A}
            goto jump_table[!(x == 2)]
LABEL_A:    do_stuff()
LABEL_B:    // ...
```

while this seems simple and easily breakable (I agree), it is enough to break IDA and Ghidra without affecting
performance.

# Mixed Boolean Arithmetic

also known as MBA Sub (Mixed Boolean Arithmetic Substitution), converts simple operations into complex ones that give
same output. it uses a pre-defined set.
example:

```c++
a ^ b = (~a & b) | (a & ~b)
b * c = (((b | c) * (b & c)) + ((b & ~c) * (c &  ~b)))
r = rand(); c = b + r; a = a + c; a = a - r
```

you can see the full list in `Passes/MBASub.cpp` if you are interested.

# Zyrox Annotations

just check `index.d.ts`. the annotation parser uses same order.
to mark a function just do the following:

```c++
__attribute__((annotate("ibr:1,100"))) void hello_world () {
    some_hello ();
}
```

annotation codes:

-   [Basic Block Splitter](#basic-block-splitter): bbs
-   [Control Flow Flattening](#control-flow-flattening): cff
-   [Indirect Branching](#indirect-branching): ibr
-   [Simple Indirect Branching](#simple-indirect-branching): sibr
-   [Mixed Boolean Arithmetic](#mixed-boolean-arithmetic): mba

example:
in `index.d.ts` we see:

```typescript
type _ = {
    "BasicBlockSplitter.SplitBlockMinSize"?: number;
    "BasicBlockSplitter.SplitBlockMaxSize"?: number;
    "BasicBlockSplitter.SplitBlockChance"?: number;
};
```

now here's the thing, first argument and the shared one for all passes is `PassIterations`, so it will be the first
arg in the annotations.
to annotate something with bbs we do:

```c++
__attribute__((annotate("bbs:1,15,30,100"))) void hello_world () {
    some_hello ();
}
```

this means: run [Basic Block Splitter](#basic-block-splitter) on `hello_world` 1 time with min size =15, max size = 30
and chance = 100.

you can also combine passes:

```c++
__attribute__((annotate("bbs:1,15,30,100 ibr:1,100 sibr:1,100"))) void hello_world () {
    some_hello ();
}
```

this means run [Basic Block Splitter](#basic-block-splitter) _then_ [Indirect Branching](#indirect-branching) and _then_
[Simple Indirect Branching](#simple-indirect-branching) on `hello_world`.
They will run by the order of definition left
to right.

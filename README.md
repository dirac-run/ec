# EasyCommand (`ec`)

Turn an English request into a Bash command, entirely on your own machine.
EasyCommand prints the command and asks before executing it.

[Models and data](https://huggingface.co/dirac-run) ·
[Research story](PROJECT_POST_DRAFT.md) ·
[ALFA-updated benchmark](https://github.com/dirac-run/ALFA-updated)

```sh
ec list the last five commits in this repository
```

The C++ application embeds llama.cpp. Inference needs a compatible GGUF model
and ordinary Linux system libraries; it does not need Python, PyTorch, a GPU,
Ollama or a network connection. Python is used only by optional setup, testing
and training utilities.

## Build and install

Requirements: Linux, Bash, a C/C++ compiler, CMake 3.20+, and network access for
the first dependency download. Build dependencies have fixed versions and
checksums. Python 3.11+ is needed for the installer and tests.

A [prebuilt Linux x86_64 bundle](https://github.com/dirac-run/ec/releases/tag/v0.1.0)
is also available, with its checksum, license notices and exact CPU/system
requirements. Build from source if your machine does not meet those requirements.

```sh
git clone https://github.com/dirac-run/ec.git
cd ec
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
python3 scripts/install
```

The installer defaults to `~/.local/bin/ec` and refuses an existing unrelated
command, including one elsewhere on `PATH`. If `ec` is already used, install
with `python3 scripts/install --name easycommand`. Direct CMake installation
also supports `cmake --install build --prefix ~/.local`, but does not perform
the installer's ownership/collision checks.

`build/easycommand` is a compatibility symlink to `build/ec`. For a build
optimized for your own CPU, add `-DEC_NATIVE=ON`; that binary may not run on
another CPU. Prebuilt binaries require their stated operating-system/CPU profile.

## Choose a model

Use an EasyCommand GGUF based on Qwen3-0.6B or Qwen2.5-Coder-1.5B-Instruct.
These are trained checkpoints; the untouched upstream models are not equivalent.
Weights are kept separately from this source repository.

| Release | Model repository | Published quants |
|---|---|---|
| 0.6B, selected A1 checkpoint | [ec-0.6b-gguf](https://huggingface.co/dirac-run/ec-0.6b-gguf) | Q4_K_M, Q8_0 |
| 1.5B, selected A3 checkpoint | [ec-1.5b-gguf](https://huggingface.co/dirac-run/ec-1.5b-gguf) | Q4_K_M |

For the 1.5B release, download and verify the model from this checkout:

```sh
python3 scripts/download-model \
  'https://huggingface.co/dirac-run/ec-1.5b-gguf/resolve/main/ec-1.5b.Q4_K_M.gguf' \
  --sha256 135c8ec1a7ad6c24ba25971ee01cb3659130676031e45d876bd1822185c9e044
ec print the system uptime
```

```sh
ec --model /path/to/model.gguf print the system uptime
ec --model /path/to/model.gguf --preview list the files in this directory
```

Without `--model`, the default is `$XDG_DATA_HOME/easycommand/model.gguf`, or
`~/.local/share/easycommand/model.gguf`. To install a downloaded model, put it
there or create a symlink to it. `scripts/download-model` accepts an HTTPS model
URL and its published SHA256 checksum; use `--help` for its arguments.

The runtime uses the exact prompt in [config/system-prompt.txt](config/system-prompt.txt),
greedy decoding, a 256-token output limit, and thinking disabled for Qwen3.
It expects COMMAND JSON and extracts the command. No JSON generation grammar
is applied. Unsupported architectures are rejected explicitly.

## Usage

`ec --help` lists the options. `--preview` prints validated JSON and executes
nothing. Ordinary requests print the proposed command; Enter, `y` or `Y`
confirms execution, and other answers cancel it. Execution requires interactive
input and output terminals. Commands are checked for Bash syntax and explicitly
named tools before the prompt. These checks do not establish that a generated
command fulfills the request; review the command before accepting it.

For repeated requests, a resident worker can keep the model in memory:

```sh
ec --serve --socket /tmp/ec.sock --model /path/to/model.gguf --threads 8
# In another terminal:
ec --socket /tmp/ec.sock --expect-model /path/to/model.gguf print the system uptime
```

The worker resets inference state between requests and verifies the requested
model identity. Remove its socket after the worker exits before starting another.
No systemd service or machine-specific shell configuration is required.

## Training and results

See [the training guide](docs/TRAINING.md) for dataset format, LoRA continuation
and GGUF conversion, and [measured results](docs/RESULTS.md) for checkpoint and
precision comparisons. ALFA-updated is a separately documented benchmark
variant; its scores are not interchangeable with original ALFA scores.
The [project post draft](PROJECT_POST_DRAFT.md) covers the data expansion,
training experiments, negative results, release choices and future directions.

Code is MIT licensed. Dependency licenses and credits are in
[THIRD_PARTY.md](THIRD_PARTY.md). Model and dataset terms are supplied separately
with their respective distributions.

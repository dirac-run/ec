# Training EasyCommand models

The command-only dataset has 401,975 distinct request/answer pairs. A row is:

```json
{"request":"list files in this directory","response":{"kind":"COMMAND","value":"ls"}}
```

The [EasyCommand training dataset](https://huggingface.co/datasets/dirac-run/ec-training-data)
is released separately under Apache-2.0. Load it with:

```python
from datasets import load_dataset

data = load_dataset("dirac-run/ec-training-data", split="train")
```

Different descriptions for the same command are retained. Exact duplicate pairs
are removed in the publication dataset. Earlier training runs used weighted
corpora with repeated presentations; one pass over the deduplicated release
dataset is not the same exposure as those historical runs.

## Model and formatting

| Family | Initial upstream model | Revision |
|---|---|---|
| 0.6B | `Qwen/Qwen3-0.6B` | `c1899de289a04d12100db370d81485cdf75e47ca` |
| 1.5B | `Qwen/Qwen2.5-Coder-1.5B-Instruct` | `2e1fd397ee46e1388853d2af2c993145b0f1098a` |

Use the original tokenizer and chat template with a system message and one user
request. For Qwen3, set `enable_thinking=False`. The assistant target is compact
COMMAND JSON followed by the assistant end token. Supervise assistant tokens
only, excluding the system/user prompt and padding. The serving prompt is in
`config/system-prompt.txt`; historical training prompts can differ and should
be disclosed in the model's training recipe.

Verify tokenizer round trips for requests and commands. Do not silently truncate
long examples or normalize spaces inside shell arguments. Check token boundaries
after rendering the complete chat template. Shuffle examples and batch order;
length bucketing reduces padding but must not make an ordered curriculum by
accident. Split by command family/intent before generating description variants,
so paraphrases of the same operation do not leak into a claimed holdout.

## Starting from trained weights

GGUF exports are inference artifacts. Continue training from a full-fidelity
Hugging Face checkpoint, or from a trained PEFT adapter with its exact upstream
model revision, adapter configuration and tokenizer. The published checkpoint
name must identify whether it starts from stock HF weights or continues an
already trained adapter. See the official
[PEFT checkpoint format](https://huggingface.co/docs/peft/main/en/developer_guides/checkpoint).

The selected [0.6B A1](https://huggingface.co/dirac-run/ec-0.6b) and
[1.5B A3](https://huggingface.co/dirac-run/ec-1.5b) releases provide merged BF16
Transformers weights and their original FP32 LoRA adapters in the adapter/
subfolder. Their cards contain loading examples and pinned upstream revisions.
Use the original upstream base with the existing adapter; putting that adapter
on the merged model would apply its learned change twice. Optimizer/RNG state
is excluded, so these releases start a new fine-tuning run from trained weights.

The deployed models use COMMAND JSON. Plain-command continuations were also
tested; they did not establish a broad improvement and are not the default
runtime format.

## Conservative pilot recipe

The successful research LoRA setup used rank 32, alpha 64, dropout 0.05 and the
attention/MLP linear projections, with a frozen BF16 base and FP32 trainable
adapters. An effective batch of 32, linear warmup and cosine decay were useful
starting points. Parent learning rates varied by architecture; the model cards
record those recipes. Short continuations compared LR `1e-5` and `5e-6`; the
selected A1/A3 releases used `5e-6` with ten warmup updates. These are starting
hypotheses, not guarantees for a different dataset, architecture or initializer.

Before a full run, compare roughly 100–200 updates against the unchanged parent
using repair cases and retention cases. Test several doses or mixtures rather
than treating one pilot as a universal optimum. Include replay of ordinary
commands: over-specializing on find improved ALFA while hurting other panels.
Record completed updates, effective batch, optimizer, target modules, loss
reduction, shuffle policy and final checkpoint identity.

Save the final full-fidelity checkpoint and adapter configuration before
quantization. Do not infer BF16 performance from Q4 or Q8: deployment precision
can change the winning checkpoint. Inspect gains and regressions, and use a
fresh confirmation set after selection. ALFA and the internal panels were
consulted during this project and must be described as development evidence,
not independent untouched test sets.

## Convert to GGUF

Use the pinned llama.cpp source fetched by the application build. Merge an
adapter into its exact upstream model before conversion, retaining the tokenizer
and configuration. The standard conversion/quantization commands are:

```sh
python3 build/_deps/llama_cpp-src/convert_hf_to_gguf.py merged_hf \
  --outfile model.F16.gguf --outtype f16
cmake -S build/_deps/llama_cpp-src -B build/quant \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DLLAMA_BUILD_TOOLS=ON -DLLAMA_BUILD_TESTS=OFF
cmake --build build/quant --target llama-quantize --parallel
build/quant/bin/llama-quantize model.F16.gguf model.Q4_K_M.gguf Q4_K_M
build/quant/bin/llama-quantize model.F16.gguf model.Q8_0.gguf Q8_0
```

Conversion requires the converter's Python dependencies; application inference
does not. Re-evaluate the resulting GGUF with the exact deployed prompt,
tokenization and decoding profile. Report Q4, Q8 and full-fidelity results
separately and publish checksums with the weight files.

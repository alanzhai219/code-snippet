import math
import random
from dataclasses import dataclass
from typing import Dict, List, Tuple


def zeros_2d(rows: int, cols: int) -> List[List[float]]:
    return [[0.0 for _ in range(cols)] for _ in range(rows)]


def zeros_3d(a: int, b: int, c: int) -> List[List[List[float]]]:
    return [[[0.0 for _ in range(c)] for _ in range(b)] for _ in range(a)]


def softmax(x: List[float]) -> List[float]:
    max_v = max(x)
    values = [math.exp(v - max_v) for v in x]
    denom = sum(values)
    return [v / denom for v in values]


def dot(a: List[float], b: List[float]) -> float:
    return sum(x * y for x, y in zip(a, b))


def linear(x: List[List[float]], w: List[List[float]]) -> List[List[float]]:
    rows = len(x)
    in_dim = len(w)
    out_dim = len(w[0])
    y = zeros_2d(rows, out_dim)
    for r in range(rows):
        for i in range(in_dim):
            xv = x[r][i]
            for j in range(out_dim):
                y[r][j] += xv * w[i][j]
    return y


@dataclass
class SequenceState:
    seq_id: int
    logical_blocks: List[int]
    past_len: int = 0


@dataclass
class BatchMetadata:
    past_lens: List[int]
    subsequence_begins: List[int]
    block_indices: List[int]
    block_indices_begins: List[int]


@dataclass
class BlockCopyPlan:
    src_block: int
    dst_block: int


class KVBlockManager:
    def __init__(self, num_blocks: int, block_size: int):
        self.num_blocks = num_blocks
        self.block_size = block_size
        self.free_blocks = list(range(num_blocks))
        self.block_ref_counts = [0 for _ in range(num_blocks)]
        self.sequences: Dict[int, SequenceState] = {}

    def add_sequence(self, seq_id: int):
        if seq_id in self.sequences:
            raise ValueError(f"sequence {seq_id} already exists")
        self.sequences[seq_id] = SequenceState(seq_id, [], 0)

    def fork_sequence(self, parent_seq_id: int, child_seq_id: int):
        if child_seq_id in self.sequences:
            raise ValueError(f"sequence {child_seq_id} already exists")
        parent = self.sequences[parent_seq_id]
        self.sequences[child_seq_id] = SequenceState(child_seq_id, list(parent.logical_blocks), parent.past_len)
        for block in parent.logical_blocks:
            self.block_ref_counts[block] += 1

    def beam_merge(self, dst_seq_id: int, src_seq_id: int):
        if dst_seq_id == src_seq_id:
            return
        dst = self.sequences[dst_seq_id]
        src = self.sequences[src_seq_id]
        self._release_sequence_blocks(dst)
        dst.logical_blocks = list(src.logical_blocks)
        dst.past_len = src.past_len
        for block in dst.logical_blocks:
            self.block_ref_counts[block] += 1

    def finish_sequence(self, seq_id: int):
        seq = self.sequences.pop(seq_id)
        self._release_sequence_blocks(seq)

    def _allocate_block(self) -> int:
        if not self.free_blocks:
            raise RuntimeError("out of KV blocks")
        block = self.free_blocks.pop(0)
        self.block_ref_counts[block] = 1
        return block

    def _release_block(self, block: int):
        ref_count = self.block_ref_counts[block]
        if ref_count <= 0:
            raise RuntimeError(f"block {block} already free")
        self.block_ref_counts[block] = ref_count - 1
        if self.block_ref_counts[block] == 0:
            self.free_blocks.append(block)
            self.free_blocks.sort()

    def _release_sequence_blocks(self, seq: SequenceState):
        for block in seq.logical_blocks:
            self._release_block(block)
        seq.logical_blocks = []
        seq.past_len = 0

    def _ensure_writable_tail(self, seq: SequenceState) -> List[BlockCopyPlan]:
        if seq.past_len == 0:
            return []
        tail_offset = seq.past_len % self.block_size
        if tail_offset == 0:
            return []

        tail_index = (seq.past_len - 1) // self.block_size
        tail_block = seq.logical_blocks[tail_index]
        if self.block_ref_counts[tail_block] <= 1:
            return []

        new_block = self._allocate_block()
        seq.logical_blocks[tail_index] = new_block
        self._release_block(tail_block)
        return [BlockCopyPlan(src_block=tail_block, dst_block=new_block)]

    def _ensure_capacity_for_append(self, seq: SequenceState, append_tokens: int):
        needed_tokens = seq.past_len + append_tokens
        needed_blocks = (needed_tokens + self.block_size - 1) // self.block_size
        while len(seq.logical_blocks) < needed_blocks:
            seq.logical_blocks.append(self._allocate_block())

    def reserve_for_prefill(self, seq_id: int, q_len: int) -> List[BlockCopyPlan]:
        seq = self.sequences[seq_id]
        copy_plans = self._ensure_writable_tail(seq) if q_len > 0 else []
        self._ensure_capacity_for_append(seq, q_len)
        return copy_plans

    def reserve_for_decode(self, seq_id: int) -> List[BlockCopyPlan]:
        return self.reserve_for_prefill(seq_id, 1)

    def commit_tokens(self, seq_id: int, num_tokens: int):
        self.sequences[seq_id].past_len += num_tokens

    def build_batch_metadata(self, seq_ids: List[int], q_lens: List[int]) -> BatchMetadata:
        past_lens: List[int] = []
        subsequence_begins = [0]
        block_indices: List[int] = []
        block_indices_begins = [0]

        token_acc = 0
        block_acc = 0
        for seq_id, q_len in zip(seq_ids, q_lens):
            seq = self.sequences[seq_id]
            past_lens.append(seq.past_len)

            token_acc += q_len
            subsequence_begins.append(token_acc)

            total_tokens = seq.past_len + q_len
            total_blocks = (total_tokens + self.block_size - 1) // self.block_size
            block_indices.extend(seq.logical_blocks[:total_blocks])

            block_acc += total_blocks
            block_indices_begins.append(block_acc)

        return BatchMetadata(past_lens, subsequence_begins, block_indices, block_indices_begins)

    def dump_state(self, seq_ids: List[int]):
        print("scheduler state:")
        for seq_id in seq_ids:
            seq = self.sequences[seq_id]
            print(f"  seq={seq_id} past_len={seq.past_len} blocks={seq.logical_blocks}")
        print(f"  free_blocks_head={self.free_blocks[:8]}")
        used_blocks = [f"{block}:{ref}" for block, ref in enumerate(self.block_ref_counts) if ref > 0]
        print(f"  block_ref_counts={used_blocks}")


class ExecutorPACommon:
    def __init__(self, block_size: int):
        self.block_size = block_size

    def build_slot_mapping(self, metadata: BatchMetadata, q_lens: List[int]) -> List[int]:
        slots: List[int] = []
        for seq_idx, q_len in enumerate(q_lens):
            past_len = metadata.past_lens[seq_idx]
            block_begin = metadata.block_indices_begins[seq_idx]
            for j in range(q_len):
                logical_pos = past_len + j
                logical_block = logical_pos // self.block_size
                offset = logical_pos % self.block_size
                physical_block = metadata.block_indices[block_begin + logical_block]
                slots.append(physical_block * self.block_size + offset)
        return slots

    def collect_context_positions(self, metadata: BatchMetadata, seq_idx: int, total_kv_len: int) -> List[Tuple[int, int]]:
        result: List[Tuple[int, int]] = []
        block_begin = metadata.block_indices_begins[seq_idx]
        for pos in range(total_kv_len):
            logical_block = pos // self.block_size
            offset = pos % self.block_size
            physical_block = metadata.block_indices[block_begin + logical_block]
            result.append((physical_block, offset))
        return result


class Int8Quantizer:
    @staticmethod
    def quantize_row(x: List[float]) -> Tuple[List[int], float]:
        max_abs = max(abs(v) for v in x) if x else 0.0
        scale = 1.0 if max_abs < 1e-12 else max_abs / 127.0
        out = []
        for v in x:
            qv = round(v / scale)
            qv = max(-127, min(127, qv))
            out.append(int(qv))
        return out, scale

    @staticmethod
    def dequantize_row(q: List[int], scale: float) -> List[float]:
        return [float(v) * scale for v in q]


class PagedAttentionExecutor:
    def __init__(
        self,
        layer_id: int,
        num_blocks: int,
        num_heads: int,
        head_size: int,
        block_size: int,
        use_int8_cache: bool = True,
    ):
        self.layer_id = layer_id
        self.num_blocks = num_blocks
        self.num_heads = num_heads
        self.head_size = head_size
        self.block_size = block_size
        self.use_int8_cache = use_int8_cache
        self.common = ExecutorPACommon(block_size)

        if use_int8_cache:
            self.k_cache = [[[[0 for _ in range(head_size)] for _ in range(block_size)] for _ in range(num_heads)] for _ in range(num_blocks)]
            self.v_cache = [[[[0 for _ in range(head_size)] for _ in range(block_size)] for _ in range(num_heads)] for _ in range(num_blocks)]
            self.k_scale = [[[1.0 for _ in range(block_size)] for _ in range(num_heads)] for _ in range(num_blocks)]
            self.v_scale = [[[1.0 for _ in range(block_size)] for _ in range(num_heads)] for _ in range(num_blocks)]
        else:
            self.k_cache = zeros_3d(num_blocks * num_heads, block_size, head_size)
            self.v_cache = zeros_3d(num_blocks * num_heads, block_size, head_size)

    def _float_cache_index(self, block: int, head: int) -> int:
        return block * self.num_heads + head

    def _write_one_token(self, slot: int, k: List[List[float]], v: List[List[float]]):
        block = slot // self.block_size
        offset = slot % self.block_size
        for h in range(self.num_heads):
            if self.use_int8_cache:
                qk, sk = Int8Quantizer.quantize_row(k[h])
                qv, sv = Int8Quantizer.quantize_row(v[h])
                self.k_cache[block][h][offset] = qk
                self.v_cache[block][h][offset] = qv
                self.k_scale[block][h][offset] = sk
                self.v_scale[block][h][offset] = sv
            else:
                idx = self._float_cache_index(block, h)
                self.k_cache[idx][offset] = list(k[h])
                self.v_cache[idx][offset] = list(v[h])

    def write_kv(self, metadata: BatchMetadata, q_lens: List[int], k_new, v_new):
        slots = self.common.build_slot_mapping(metadata, q_lens)
        for token_idx, slot in enumerate(slots):
            self._write_one_token(slot, k_new[token_idx], v_new[token_idx])

    def copy_block(self, src_block: int, dst_block: int):
        for h in range(self.num_heads):
            for offset in range(self.block_size):
                if self.use_int8_cache:
                    self.k_cache[dst_block][h][offset] = list(self.k_cache[src_block][h][offset])
                    self.v_cache[dst_block][h][offset] = list(self.v_cache[src_block][h][offset])
                    self.k_scale[dst_block][h][offset] = self.k_scale[src_block][h][offset]
                    self.v_scale[dst_block][h][offset] = self.v_scale[src_block][h][offset]
                else:
                    src_idx = self._float_cache_index(src_block, h)
                    dst_idx = self._float_cache_index(dst_block, h)
                    self.k_cache[dst_idx][offset] = list(self.k_cache[src_idx][offset])
                    self.v_cache[dst_idx][offset] = list(self.v_cache[src_idx][offset])

    def _read_token_kv(self, block: int, offset: int) -> Tuple[List[List[float]], List[List[float]]]:
        k = zeros_2d(self.num_heads, self.head_size)
        v = zeros_2d(self.num_heads, self.head_size)
        for h in range(self.num_heads):
            if self.use_int8_cache:
                k[h] = Int8Quantizer.dequantize_row(self.k_cache[block][h][offset], self.k_scale[block][h][offset])
                v[h] = Int8Quantizer.dequantize_row(self.v_cache[block][h][offset], self.v_scale[block][h][offset])
            else:
                idx = self._float_cache_index(block, h)
                k[h] = list(self.k_cache[idx][offset])
                v[h] = list(self.v_cache[idx][offset])
        return k, v

    def _attention_one_sequence(self, q_seq, metadata: BatchMetadata, seq_idx: int, total_kv_len: int):
        ctx_positions = self.common.collect_context_positions(metadata, seq_idx, total_kv_len)

        all_k = []
        all_v = []
        for block, offset in ctx_positions:
            k_tok, v_tok = self._read_token_kv(block, offset)
            all_k.append(k_tok)
            all_v.append(v_tok)

        q_len = len(q_seq)
        out = zeros_3d(q_len, self.num_heads, self.head_size)
        scale = 1.0 / math.sqrt(self.head_size)
        for t in range(q_len):
            causal_kv_len = total_kv_len - (q_len - 1 - t)
            for h in range(self.num_heads):
                scores = [dot(q_seq[t][h], all_k[idx][h]) * scale for idx in range(causal_kv_len)]
                probs = softmax(scores)
                for idx in range(causal_kv_len):
                    for d in range(self.head_size):
                        out[t][h][d] += probs[idx] * all_v[idx][h][d]
        return out

    def prefill(self, metadata: BatchMetadata, q_lens: List[int], q, k, v):
        self.write_kv(metadata, q_lens, k, v)
        outputs = []
        token_start = 0
        for seq_idx, q_len in enumerate(q_lens):
            token_end = token_start + q_len
            total_kv_len = metadata.past_lens[seq_idx] + q_len
            outputs.extend(self._attention_one_sequence(q[token_start:token_end], metadata, seq_idx, total_kv_len))
            token_start = token_end
        return outputs

    def decode(self, metadata: BatchMetadata, q, k, v):
        q_lens = [1] * len(metadata.past_lens)
        self.write_kv(metadata, q_lens, k, v)
        outputs = []
        for seq_idx in range(len(q_lens)):
            total_kv_len = metadata.past_lens[seq_idx] + 1
            outputs.extend(self._attention_one_sequence(q[seq_idx:seq_idx + 1], metadata, seq_idx, total_kv_len))
        return outputs


class ToyLayer:
    def __init__(
        self,
        layer_id: int,
        hidden_size: int,
        num_heads: int,
        head_size: int,
        num_blocks: int,
        block_size: int,
        use_int8_cache: bool,
        seed: int,
    ):
        self.layer_id = layer_id
        self.hidden_size = hidden_size
        self.num_heads = num_heads
        self.head_size = head_size

        rand = random.Random(seed)
        proj_size = num_heads * head_size
        scale = 1.0 / math.sqrt(hidden_size)

        def make_matrix(rows: int, cols: int) -> List[List[float]]:
            return [[rand.gauss(0.0, scale) for _ in range(cols)] for _ in range(rows)]

        self.wq = make_matrix(hidden_size, proj_size)
        self.wk = make_matrix(hidden_size, proj_size)
        self.wv = make_matrix(hidden_size, proj_size)
        self.wo = make_matrix(proj_size, hidden_size)

        self.pa = PagedAttentionExecutor(layer_id, num_blocks, num_heads, head_size, block_size, use_int8_cache)

    def _project_qkv(self, x):
        q = linear(x, self.wq)
        k = linear(x, self.wk)
        v = linear(x, self.wv)
        return self._split_heads(q), self._split_heads(k), self._split_heads(v)

    def _split_heads(self, x):
        out = zeros_3d(len(x), self.num_heads, self.head_size)
        for t in range(len(x)):
            for h in range(self.num_heads):
                for d in range(self.head_size):
                    out[t][h][d] = x[t][h * self.head_size + d]
        return out

    def _merge_heads(self, x):
        out = zeros_2d(len(x), self.num_heads * self.head_size)
        for t in range(len(x)):
            for h in range(self.num_heads):
                for d in range(self.head_size):
                    out[t][h * self.head_size + d] = x[t][h][d]
        return out

    def forward_prefill(self, x, metadata: BatchMetadata, q_lens: List[int]):
        q, k, v = self._project_qkv(x)
        attn_out = self.pa.prefill(metadata, q_lens, q, k, v)
        return linear(self._merge_heads(attn_out), self.wo)

    def forward_decode(self, x, metadata: BatchMetadata):
        q, k, v = self._project_qkv(x)
        attn_out = self.pa.decode(metadata, q, k, v)
        return linear(self._merge_heads(attn_out), self.wo)

    def copy_block(self, src_block: int, dst_block: int):
        self.pa.copy_block(src_block, dst_block)


class ToyLLMRuntime:
    def __init__(
        self,
        num_layers: int,
        hidden_size: int,
        num_heads: int,
        head_size: int,
        num_blocks: int,
        block_size: int,
        use_int8_cache: bool = True,
    ):
        self.num_layers = num_layers
        self.hidden_size = hidden_size
        self.manager = KVBlockManager(num_blocks, block_size)
        self.layers = [
            ToyLayer(i, hidden_size, num_heads, head_size, num_blocks, block_size, use_int8_cache, 1234 + i)
            for i in range(num_layers)
        ]
        self.prefill_done = False

    def add_sequence(self, seq_id: int):
        self.manager.add_sequence(seq_id)

    def fork_sequence(self, parent_seq_id: int, child_seq_id: int):
        self.manager.fork_sequence(parent_seq_id, child_seq_id)

    def beam_merge(self, dst_seq_id: int, src_seq_id: int):
        self.manager.beam_merge(dst_seq_id, src_seq_id)

    def finish_sequence(self, seq_id: int):
        self.manager.finish_sequence(seq_id)

    def _apply_copy_plans(self, copy_plans: List[BlockCopyPlan]):
        for plan in copy_plans:
            for layer in self.layers:
                layer.copy_block(plan.src_block, plan.dst_block)

    def prefill(self, seq_ids: List[int], x, q_lens: List[int]):
        if self.prefill_done:
            raise RuntimeError("prefill already executed")
        copy_plans: List[BlockCopyPlan] = []
        for seq_id, q_len in zip(seq_ids, q_lens):
            copy_plans.extend(self.manager.reserve_for_prefill(seq_id, q_len))
        self._apply_copy_plans(copy_plans)
        metadata = self.manager.build_batch_metadata(seq_ids, q_lens)

        hidden = x
        for layer in self.layers:
            hidden = layer.forward_prefill(hidden, metadata, q_lens)

        for seq_id, q_len in zip(seq_ids, q_lens):
            self.manager.commit_tokens(seq_id, q_len)
        self.prefill_done = True
        return hidden

    def decode(self, seq_ids: List[int], x):
        if not self.prefill_done:
            raise RuntimeError("decode called before prefill")
        q_lens = [1] * len(seq_ids)
        copy_plans: List[BlockCopyPlan] = []
        for seq_id in seq_ids:
            copy_plans.extend(self.manager.reserve_for_decode(seq_id))
        self._apply_copy_plans(copy_plans)
        metadata = self.manager.build_batch_metadata(seq_ids, q_lens)

        hidden = x
        for layer in self.layers:
            hidden = layer.forward_decode(hidden, metadata)

        for seq_id in seq_ids:
            self.manager.commit_tokens(seq_id, 1)
        return hidden


def make_random_tensor2(rows: int, cols: int, seed: int):
    rand = random.Random(seed)
    return [[rand.gauss(0.0, 1.0) for _ in range(cols)] for _ in range(rows)]


def shape_2d(x) -> Tuple[int, int]:
    return len(x), (len(x[0]) if x else 0)


def demo():
    runtime = ToyLLMRuntime(
        num_layers=4,
        hidden_size=32,
        num_heads=4,
        head_size=8,
        num_blocks=64,
        block_size=4,
        use_int8_cache=True,
    )

    seq_ids = [100, 200]
    for seq_id in seq_ids:
        runtime.add_sequence(seq_id)

    print("=== initial state ===")
    runtime.manager.dump_state(seq_ids)

    print("\n=== prefill ===")
    prefill_q_lens = [3, 2]
    x_prefill = make_random_tensor2(sum(prefill_q_lens), 32, 1)
    out_prefill = runtime.prefill(seq_ids, x_prefill, prefill_q_lens)
    print("prefill output shape:", shape_2d(out_prefill))
    runtime.manager.dump_state(seq_ids)

    print("\n=== decode step 1 ===")
    x_decode_1 = make_random_tensor2(len(seq_ids), 32, 2)
    out_decode_1 = runtime.decode(seq_ids, x_decode_1)
    print("decode step 1 output shape:", shape_2d(out_decode_1))
    runtime.manager.dump_state(seq_ids)

    print("\n=== decode step 2 ===")
    x_decode_2 = make_random_tensor2(len(seq_ids), 32, 3)
    out_decode_2 = runtime.decode(seq_ids, x_decode_2)
    print("decode step 2 output shape:", shape_2d(out_decode_2))
    runtime.manager.dump_state(seq_ids)

    print("\n=== fork beam: 100 -> 300 ===")
    runtime.fork_sequence(100, 300)
    runtime.manager.dump_state([100, 200, 300])

    print("\n=== branch decode after fork ===")
    x_decode_3 = make_random_tensor2(2, 32, 4)
    out_decode_3 = runtime.decode([100, 300], x_decode_3)
    print("branch decode output shape:", shape_2d(out_decode_3))
    runtime.manager.dump_state([100, 200, 300])

    print("\n=== beam merge: 200 <- 300 ===")
    runtime.beam_merge(200, 300)
    runtime.manager.dump_state([100, 200, 300])

    print("\n=== finish sequence 300 ===")
    runtime.finish_sequence(300)
    runtime.manager.dump_state([100, 200])

    print("\n=== finish remaining sequences ===")
    runtime.finish_sequence(100)
    runtime.finish_sequence(200)
    print("scheduler state:")
    print(f"  free_blocks_head={runtime.manager.free_blocks[:8]}")
    used_blocks = [f"{block}:{ref}" for block, ref in enumerate(runtime.manager.block_ref_counts) if ref > 0]
    print(f"  block_ref_counts={used_blocks}")


if __name__ == "__main__":
    demo()

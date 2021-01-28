import os
from typing import Dict, Optional, Tuple

from util import parse_step, prepend_step
from types_ import *

FidlDefs = Dict[FidlRef, FidlDef]


def reverse_test(test: CompatTest) -> Tuple[CompatTest, Dict[str, str]]:
    """
    Returns a new CompatTest representing the reverse of the provided test,
    as well as a mapping from each file in the input test to its new name
    in the output test (for example, hlcpp/before.cc might become hlcpp/after.cc
    in the reversed test). Each entry is a path relative to the test root.
    """
    title = test.title
    max_step_num = get_num_steps(test)
    fidl, old_to_new_fidl = reverse_fidl_steps(test.fidl, max_step_num)

    bindings: [str, Steps] = {}
    old_to_new: [str, str] = {}
    for b, steps in test.bindings.items():
        rev_steps, old_to_new_src = reverse_src_steps(
            steps, old_to_new_fidl, max_step_num)
        bindings[b] = rev_steps
        old_to_new.update(old_to_new_src)

    # convert from FidlRefs to full source paths
    old_to_new_fidl = {
        test.fidl[old].source: fidl[new].source
        for old, new in old_to_new_fidl.items()
    }
    old_to_new.update(old_to_new_fidl)

    return CompatTest(title, fidl, bindings), old_to_new


def reverse_fidl_steps(fidl: FidlDefs, max_step_num: int) -> (
        FidlDefs, Dict[FidlRef, FidlRef]):
    # we assume that alphabetical ordering will match chronological ordering
    # (this is the case based on current naming conventions, e.g. step_0N_foo)
    refs = sorted(fidl.keys())
    reversed_refs = reverse_steps(refs, max_step_num)
    instructions = reverse_instructions(
        [list(s.instructions) for s in fidl.values()])

    # we hardcode this below so double check that the input paths have the
    # expected format
    for s in fidl.values():
        assert s.source.startswith('fidl/') and s.source.endswith('.test.fidl')

    result: FidlDefs = {}
    old_to_new: Dict[FidlRef, FidlRef] = {}
    for old_name, new_name, instructions in zip(reversed(refs), reversed_refs,
                                                instructions):
        result[new_name] = FidlDef(f'fidl/{new_name}.test.fidl', instructions)
        old_to_new[old_name] = new_name
    return result, old_to_new


def reverse_src_steps(
        transition: Steps, old_to_new_fidl: Dict[FidlRef, FidlRef],
        max_step_num: int) -> (Steps, Dict[str, str]):
    # accumulate all src step paths
    src_steps: List[str] = [transition.starting_src]
    src_steps.extend(
        [s.source for s in transition.steps if isinstance(s, SourceStep)])

    # get a mapping from old path to new path (each key value pair refers to the
    # same source file)
    reversed_steps = reverse_steps(src_steps, max_step_num)
    old_to_new_src = dict(zip(reversed(src_steps), reversed_steps))

    # accumulate all steps in a list (starting fidl/src order is determined by
    assert transition.steps, 'transition has at least one change'
    all_steps = [transition.starting_fidl, transition.starting_src
                ] if isinstance(transition.steps[0], FidlStep) else [
                    transition.starting_src, transition.starting_fidl
                ]
    all_steps.extend(
        [
            s.source if isinstance(s, SourceStep) else s.fidl
            for s in transition.steps
        ])

    if all_steps[-1] in old_to_new_fidl:
        starting_fidl = old_to_new_fidl[all_steps.pop()]
        starting_src = old_to_new_src[all_steps.pop()]
    else:
        starting_src = old_to_new_src[all_steps.pop()]
        starting_fidl = old_to_new_fidl[all_steps.pop()]
    reversed_steps = []
    instructions = reversed(
        [s.instructions for s in transition.steps if isinstance(s, SourceStep)])
    for step in reversed(all_steps):
        if step in old_to_new_fidl:
            new = old_to_new_fidl[step]
            reversed_steps.append(
                FidlStep(fidl=new, step_num=parse_step(new)[1]))
        else:
            new = old_to_new_src[step]
            reversed_steps.append(
                SourceStep(
                    source=new,
                    step_num=parse_step(new)[1],
                    instructions=next(instructions)))
    return Steps(starting_fidl, starting_src, reversed_steps), old_to_new_src


def reverse_steps(steps: List[str], max_step_num: int) -> List[int]:
    """
    Takes a list of strings composed of step number and names, and returns what
    they should be when reversed, e.g.
    >>> reverse_steps(['step_00_foo', 'step_01_bar', 'step_03_baz'], 3)
    ['step_00_baz', 'step_02_bar', 'step_03_foo']
    This function will preserve any leading parts of a path and any file
    extensions.
    """
    # separate out just the filenames
    heads, steps = zip(*[os.path.split(s) for s in steps])

    # separate out step numbers from names
    names, step_nums = zip(*[parse_step(s) for s in steps])
    new_step_nums = reverse_step_nums(step_nums, max_step_num)
    # merge the step numbers and names back together
    result = [prepend_step(s, n) for s, n in zip(names, new_step_nums)]

    # re-prepend the paths back onto the reversed filenames
    return [os.path.join(h, r) for h, r in zip(reversed(heads), result)]


def reverse_step_nums(nums: List[int], max_step_num: int) -> List[int]:
    assert nums[
        0] == 0, 'every sequence of steps needs to have an initial state'
    return [0] + [max_step_num + 1 - n for n in reversed(nums[1:])]


def reverse_instructions(ins: List[List[str]]) -> List[List[str]]:
    assert ins[0] == [], 'initial state shouldnt have associated instructions'
    return [[]] + list(reversed(ins[1:]))


def get_num_steps(test: CompatTest) -> int:
    all_steps = [s for steps in test.bindings.values() for s in steps.steps]
    return max([s.step_num for s in all_steps])

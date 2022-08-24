from typing import TYPE_CHECKING, Dict, List, Tuple

import ert.data
import ert.storage
from ert.ert3 import algorithms

from ._entity import TransmitterCoroutine

if TYPE_CHECKING:
    from ert.ert3.config import ExperimentConfig, LinkedInput, ParametersConfig
    from ert.ert3.stats import Distribution
    from ert.ert3.workspace import Workspace


# pylint: disable=too-many-arguments
def analyze_sensitivity(
    stochastic_inputs: Tuple["LinkedInput", ...],
    experiment_config: "ExperimentConfig",
    parameters_config: "ParametersConfig",
    workspace: "Workspace",
    experiment_name: str,
    model_output: Dict[int, Dict[str, ert.data.RecordTransmitter]],
) -> None:
    if experiment_config.algorithm == "one-at-a-time":
        # There is no post analysis step for the one-at-a-time algorithm
        pass
    elif experiment_config.algorithm == "fast":
        sensitivity_parameters = _load_sensitivity_parameters(
            stochastic_inputs, parameters_config
        )
        analysis = algorithms.fast_analyze(
            sensitivity_parameters, model_output, experiment_config.harmonics
        )
        workspace.export_json(
            experiment_name,
            analysis,
            output_file=f"{experiment_name}_fast_analysis.json",
        )

    else:
        raise ValueError(
            "Unable to determine analysis step "
            f"for algorithm {experiment_config.algorithm}"
        )


def transmitter_map_sensitivity(
    stochastic_inputs: Tuple["LinkedInput", ...],
    sensitivity_records: List[Dict[str, ert.data.Record]],
    experiment_name: str,
    workspace: "Workspace",
) -> List[TransmitterCoroutine]:
    sensitivity_parameters: Dict[str, List[ert.data.Record]] = {
        input_.name: [] for input_ in stochastic_inputs
    }

    for realization in sensitivity_records:
        assert sensitivity_parameters.keys() == realization.keys()
        for record_name in realization:
            sensitivity_parameters[record_name].append(realization[record_name])

    futures: List[TransmitterCoroutine] = []
    for record_name in sensitivity_parameters:
        ensemble_record = ert.data.RecordCollection(
            records=tuple(sensitivity_parameters[record_name])
        )
        future = ert.storage.transmit_record_collection(
            record_coll=ensemble_record,
            record_name=record_name,
            workspace_name=workspace.name,
            experiment_name=experiment_name,
        )
        futures.append(future)
    return futures


def _load_sensitivity_parameters(
    stochastic_inputs: Tuple["LinkedInput", ...],
    parameters_config: "ParametersConfig",
) -> Dict[str, "Distribution"]:
    all_distributions = {
        param.name: param.as_distribution() for param in parameters_config
    }

    sensitivity_parameters = {}
    for input_ in stochastic_inputs:
        parameter_name = input_.source_location
        sensitivity_parameters[input_.name] = all_distributions[parameter_name]
    return sensitivity_parameters


def prepare_sensitivity(
    stochastic_inputs: Tuple["LinkedInput", ...],
    experiment_config: "ExperimentConfig",
    parameters_config: "ParametersConfig",
) -> List[Dict[str, ert.data.Record]]:
    sensitivity_distributions = _load_sensitivity_parameters(
        stochastic_inputs, parameters_config
    )

    if experiment_config.algorithm == "one-at-a-time":
        sensitivity_input_records = algorithms.one_at_a_time(
            sensitivity_distributions, tail=experiment_config.tail
        )
    elif experiment_config.algorithm == "fast":
        sensitivity_input_records = algorithms.fast_sample(
            sensitivity_distributions,
            experiment_config.harmonics,
            experiment_config.sample_size,
        )
    else:
        raise ValueError(f"Unknown algorithm {experiment_config.algorithm}")
    return sensitivity_input_records

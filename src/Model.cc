#include <fstream>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include "Model.h"
#include "operations/OperationFactory.h"

Model::Model(std::string onnx_path, json model_config, SimulationConfig config, std::string name) {
  std::ifstream model_istream(onnx_path);
  google::protobuf::io::IstreamInputStream zero_copy_input(&model_istream);
  _model_proto.ParseFromZeroCopyStream(&zero_copy_input) && model_istream.eof();
  _name = name;
  _root_node_id = generate_id();
  _config = config;
  _model_config = model_config;
  auto input = _model_proto.graph().input();

  for (auto iter: input) {
    std::vector<uint32_t> input_dim;
    std::string input_name = iter.name();
    auto input_shape = iter.type().tensor_type().shape();

    /* Parsing input tensor shape */
    for (int dim_idx=0; dim_idx<input_shape.dim_size(); dim_idx++) {
      /* Get axis, dynamic axis */
      int dim_value = input_shape.dim(dim_idx).dim_value();
      std::string dim_param = input_shape.dim(dim_idx).dim_param();
      spdlog::debug("input name: {} val: {} param: {}", input_name, dim_value, dim_param);
      if (dim_value==0 && dim_param!="") {
        /* Dynamic axis */
        input_dim.push_back(_model_config[dim_param]);
      } else {
        input_dim.push_back(dim_value);
      }
    }

    /* NCHW to NHWC convert */
    if (input.size()==1 && input_dim.size()==4 && input_dim.at(2)==input_dim.at(3)) {
      uint32_t channel = input_dim.at(1);
      input_dim.erase(input_dim.begin() + 1);
      input_dim.push_back(channel);
    }

    auto input_tensor = std::make_unique<Tensor>(_root_node_id, input_name, input_dim, true);
    int id = input_tensor->get_id();
    input_tensor->allocate_tensor(_config.precision * 16);
    input_tensor->set_produced();
    _tensor_map[id] = std::move(input_tensor);
  }

  for(auto initializer : _model_proto.graph().initializer()) {
    //initialize weights
    auto tensor = std::make_unique<Tensor>(_root_node_id, initializer, true);
    tensor->allocate_tensor(config.precision);
    uint32_t id = tensor->get_id();
    _tensor_map[id] = std::move(tensor);
  }
}

Model::Model(const Model& model) {
  _name = model._name;
  _model_proto = model._model_proto;
  _root_node_id = _root_node_id;
  _input_tensor = _input_tensor;
  
  for(auto const& [key, val] : model._tensor_map) {
    _tensor_map[key] = std::make_unique<Tensor>(*val.get());
  }
  for(auto const& [key, val] : model._operation_map) {
    _operation_map[key] = OperationFactory::copy_operation(val.get());
    _operation_map[key]->_model = this;
  }
  for(auto layer : model._executable_layer) {
    _executable_layer.push_back(_operation_map[layer->get_id()].get());
    spdlog::trace("add op {0:x}", fmt::ptr(_executable_layer.front()));
  }
}

Tensor* Model::get_tensor(uint32_t id) {
  return _tensor_map[id].get();
}

Tensor* Model::find_tensor(std::string name) {
  for(auto const& [key, val]: _tensor_map) {
    if(val->_name == name) {
      return val.get();
    }
  }
  return nullptr;
}

void Model::add_tensor(std::unique_ptr<Tensor> edge) {
  _tensor_map[edge->get_id()] = std::move(edge);
}
int skip_count = 0;
void Model::initialize_model(MappingTable mapping_table) {
  std::vector<std::unique_ptr<Tensor>> input_tensors;

  for(auto node_proto : _model_proto.graph().node()) {
    auto node = OperationFactory::create_operation(this, node_proto);
    if(node != nullptr) {
      int node_id = node->get_id();
      _operation_map[node->get_id()] = std::move(node);
    }
  }

  for (auto& [key, val]: _operation_map) {
    if(val->check_executable()) {
      spdlog::debug("runnable op, {}", val->get_optype());
      _executable_layer.push_back(val.get());
    } 
  }

  for(auto& [key, val] : _operation_map) {
    val->initialize_tiles(mapping_table);
  }
}


void Model::set_layer_finish(uint32_t id) {
  _operation_map[id]->set_finish();
  for(auto iter = _executable_layer.begin(); iter != _executable_layer.end(); iter ++) {
    if(id == (*iter)->get_id()) {
      _executable_layer.erase(iter);
      break;
    }
  }
  for(auto op_id : _operation_map[id]->get_child_nodes()) {
    Operation* op = _operation_map[op_id].get();
    if(op->check_executable() && !check_exist_in_exeutable(op->get_id()))  {
      _executable_layer.push_back(op);
    }
  }

}

std::vector<Operation*> Model::get_executable_layers() {
  return _executable_layer;
}

bool Model::check_finish() {
  bool finish = true;
  for(auto const& [key, val] : _operation_map) {
    finish = finish && val->check_finish();
  }
  return finish;
}

bool Model::check_exist_in_exeutable(uint32_t op_id) {
  for(auto iter = _executable_layer.begin(); iter != _executable_layer.end(); iter ++) {
    if(op_id == (*iter)->get_id()) {
      return true;
    }
  }
  return false;
}
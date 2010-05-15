#ifndef EFFECT_WRAPPER_HPP
#define EFFECT_WRAPPER_HPP

#include "Logger.hpp"
#include "error2.hpp"

#include <hash_map>

class EffectWrapper
{
public:
  EffectWrapper();
  ~EffectWrapper();

	bool	load_vertex_shader(const char* filename, const char* entry_point);
	bool	load_pixel_shader(const char* filename, const char* entry_point);

  // load both vertex and pixel shader
  bool  load_shaders(const char *filename, const char *vs, const char *ps);

  void  set_shaders(ID3D11DeviceContext *context);

  template<typename T> 
  bool	set_vs_variable(const std::string& name, const T& value) { return _vs.set_variable<T>(name, value); }

  template<typename T> 
  bool	set_ps_variable(const std::string& name, const T& value) { return _ps.set_variable<T>(name, value); }

  void  unmap_buffers();
  bool  set_resource(const std::string& name, ID3D11ShaderResourceView* resource);

	void set_cbuffer();

	ID3D11InputLayout*	create_input_layout(const std::vector<D3D11_INPUT_ELEMENT_DESC>& elems);
	ID3D11InputLayout*	create_input_layout(const D3D11_INPUT_ELEMENT_DESC* elems, const int num_elems);

	ID3D11VertexShader* vertex_shader() { return _vs._shader; }
	ID3D11PixelShader* pixel_shader() { return _ps._shader; }

private:

  struct ConstantBuffer
  {
    ConstantBuffer(const std::string& name, ID3D11Buffer* buffer, const D3D11_BUFFER_DESC& desc)
      : _name(name), _mapped(false), _desc(desc) 
    {
      _buffer.Attach(buffer);
    }
    std::string _name;
    bool _mapped;
    D3D11_MAPPED_SUBRESOURCE _resource;
    CComPtr<ID3D11Buffer> _buffer;
    D3D11_BUFFER_DESC _desc;
  };

  struct BufferVariable
  {
    BufferVariable(const std::string& name, ConstantBuffer* buffer, const D3D11_SHADER_VARIABLE_DESC& vd, const D3D11_SHADER_TYPE_DESC& td)
      : _name(name), _buffer(buffer), _var_desc(vd), _type_desc(td)
    {
    }
    std::string _name;
    ConstantBuffer* _buffer;
    D3D11_SHADER_VARIABLE_DESC _var_desc;
    D3D11_SHADER_TYPE_DESC _type_desc;
  };

  typedef std::string BufferName;
  typedef std::string VariableName;
  typedef std::map< BufferName, ConstantBuffer* > ConstantBuffers;
  typedef stdext::hash_map< VariableName, BufferVariable* > BufferVariables;
	typedef stdext::hash_map< std::string, D3D11_SHADER_INPUT_BIND_DESC > BoundTextures;
	typedef stdext::hash_map< std::string, D3D11_SHADER_INPUT_BIND_DESC > BoundSamplers;

	bool	load_inner(const char* filename, const char* entry_point, bool vertex_shader);
	bool do_reflection();

  std::string _filename;

  template<class T>
  struct Shader
  {
    ~Shader()
    {
      map_delete(_buffer_variables);
      map_delete(_constant_buffers);
    }

    bool do_reflection()
    {
      ID3D11ShaderReflection* reflector = NULL; 
      RETURN_ON_FAIL_BOOL(D3DReflect(_blob->GetBufferPointer(), _blob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector),
        ErrorPredicate<HRESULT>, LOG_ERROR_LN);
      D3D11_SHADER_DESC shader_desc;
      reflector->GetDesc(&shader_desc);

      D3D11_SHADER_BUFFER_DESC d;
      D3D11_SHADER_VARIABLE_DESC vd;
      D3D11_SHADER_TYPE_DESC td;

      // global constant buffer is called "$Globals"
      for (UINT i = 0; i < shader_desc.ConstantBuffers; ++i) {
        ID3D11ShaderReflectionConstantBuffer* rcb = reflector->GetConstantBufferByIndex(i);
        rcb->GetDesc(&d);
        CD3D11_BUFFER_DESC bb(d.Size, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);

        // check if the constant buffer has already been created
        ConstantBuffer* cur_cb = NULL;
        bool created = false;
        auto it = _constant_buffers.find(d.Name);
        if (it != _constant_buffers.end()) {
          cur_cb = it->second;
        } else {
          ID3D11Buffer *cb = NULL;
          RETURN_ON_FAIL_BOOL(Graphics::instance().device()->CreateBuffer(&bb, NULL, &cb), ErrorPredicate<HRESULT>, LOG_ERROR_LN);
          cur_cb = new ConstantBuffer(d.Name, cb, bb);
          created = true;
        }

        for (UINT j = 0; j < d.Variables; ++j) {
          ID3D11ShaderReflectionVariable* v = rcb->GetVariableByIndex(j);
          v->GetDesc(&vd);
          if (_buffer_variables.find(vd.Name) == _buffer_variables.end()) {
            ID3D11ShaderReflectionType* t = v->GetType();
            t->GetDesc(&td);
            _buffer_variables.insert(std::make_pair(vd.Name, new BufferVariable(vd.Name, cur_cb, vd, td)));
          }
        }
        if (created)
          _constant_buffers.insert(std::make_pair(cur_cb->_name, cur_cb ));
      }

      // get bound resources
      for (UINT i = 0; i < shader_desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC input_desc;
        reflector->GetResourceBindingDesc(i, &input_desc);
        switch (input_desc.Type) {
        case D3D10_SIT_TEXTURE:
          _bound_textures.insert(std::make_pair(input_desc.Name, input_desc));
          break;
        case D3D10_SIT_SAMPLER:
          _bound_samplers.insert(std::make_pair(input_desc.Name, input_desc));
          break;

        }
      }
      return true;
    }

    template<typename U> bool set_variable(const std::string& name, const U& value)
    {
      // find variable
      BufferVariables::iterator it = _buffer_variables.find(name);
      if (it == _buffer_variables.end()) {
        LOG_WARNING_LN("Variable not found: %s", name.c_str());
        return false;
      }

      BufferVariable* var = it->second;
      // check the size
      if (var->_var_desc.Size != sizeof(U)) {
        LOG_WARNING_LN("Variable size doesn't match: %s", name.c_str());
        return false;
      }

      // map the buffer
      if (!var->_buffer->_mapped) {
        var->_buffer->_mapped = true;
        Graphics::instance().context()->Map(var->_buffer->_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &var->_buffer->_resource);
      }

      // set
      memcpy((uint8_t*)var->_buffer->_resource.pData + var->_var_desc.StartOffset, &value, sizeof(U));

      return true;
    }

    void unmap_buffers()
    {
      ID3D11DeviceContext* context = Graphics::instance().context();
      for (ConstantBuffers::iterator i = _constant_buffers.begin(), e = _constant_buffers.end(); i != e; ++i) {
        ConstantBuffer* b = i->second;
        if (b->_mapped) {
          b->_mapped = false;
          context->Unmap(b->_buffer, 0);
        }
      }
    }

    BoundTextures _bound_textures;
    BoundSamplers _bound_samplers;

    ConstantBuffers _constant_buffers;
    BufferVariables _buffer_variables;
    CComPtr<ID3DBlob> _blob;
    CComPtr<T> _shader;
  };


  Shader<ID3D11VertexShader> _vs;
  Shader<ID3D11PixelShader> _ps;
};

#endif
#include "Base.h"
#include "Pass.h"
#include "Technique.h"
#include "Material.h"
#include "Node.h"

namespace vkcore
{

Pass::Pass(const char* id, Technique* technique) :
    _id(id ? id : ""), _technique(technique), _effect(NULL)
{
    RenderState::_parent = _technique;
}

Pass::~Pass()
{
    SAFE_RELEASE(_effect);
}

bool Pass::initialize(const char* vshPath, const char* fshPath, const char* defines)
{
    GP_ASSERT(vshPath);
    GP_ASSERT(fshPath);

    SAFE_RELEASE(_effect);

    // Attempt to create/load the effect.
    _effect = Effect::createFromFile(vshPath, fshPath, defines);
    if (_effect == NULL)
    {
        GP_WARN("Failed to create effect for pass. vertexShader = %s, fragmentShader = %s, defines = %s", vshPath, fshPath, defines ? defines : "");
        return false;
    }

    return true;
}

const char* Pass::getId() const
{
    return _id.c_str();
}

Effect* Pass::getEffect() const
{
    return _effect;
}


void Pass::bind()
{
    GP_ASSERT(_effect);

    // Bind our effect.
    _effect->bind();

    // Bind our render state
    RenderState::bind(this);

}

void Pass::unbind()
{

}

Pass* Pass::clone(Technique* technique, NodeCloneContext &context) const
{
    GP_ASSERT(_effect);
    _effect->addRef();

    Pass* pass = new Pass(getId(), technique);
    pass->_effect = _effect;

    RenderState::cloneInto(pass, context);
    pass->_parent = technique;
    return pass;
}

}

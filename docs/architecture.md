# Architecture

## Components

```text
ZW3DAgentPlugin
  owns chat command and Cidu interaction
  depends on shared CiduClient and ModelParameters

CiduParametricEngine
  internal model generation engine loaded by the Agent plugin
  depends on shared Zw3DHost and ModelParameters

cidu_zw3d_shared
  Cidu HTTP client
  minimal JSON protocol helpers
  plugin config loader
  ZW3D SDK boundary
```

## Plugin Boundary

The Agent DLL is registered with ZW3D. The internal engine DLL exposes C ABI
entry points so the Agent can call it without depending on C++ name mangling:

```text
CiduZW3DParametricInitialize
CiduZW3DParametricShutdown
CiduZW3DGenerateModelFromJson

CiduZW3DAgentInitialize
CiduZW3DAgentShutdown
CiduZW3DAskAndGenerate
```

Once the real ZW3D SDK entry signatures are known, these exported functions can
be adapted while preserving the inner service classes.

## Cidu Contract

The agent should be prompted to return model parameters as JSON:

```json
{
  "template_name": "box",
  "numeric": {
    "length": 100,
    "width": 60,
    "height": 30
  },
  "text": {
    "material": "steel"
  }
}
```

The current `AgentService` reads the Cidu `answer` field and attempts to parse
that value as the parameter JSON.

## Next Steps

1. Create a Cidu app whose answer is strict JSON matching
   `config/model_parameter_schema.json`.
2. Add real ZW3D SDK include/lib paths.
3. Replace `Zw3DHost::generate_model` with actual template lookup, parameter
   assignment, regeneration, and save/export logic.
4. Add a small sample template and parameter fixture once a ZW3D SDK/dev
   machine is available.

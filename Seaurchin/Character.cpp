#include "Character.h"
#include "ExecutionManager.h"
#include "Result.h"
#include "Misc.h"

using namespace std;

void RegisterCharacterTypes(ExecutionManager *exm)
{
    auto engine = exm->GetScriptInterfaceUnsafe()->GetEngine();

    engine->RegisterInterface(SU_IF_ABILITY);
    engine->RegisterInterfaceMethod(SU_IF_ABILITY, "void Initialize(dictionary@)");
    engine->RegisterInterfaceMethod(SU_IF_ABILITY, "void OnStart(" SU_IF_RESULT "@)");
    engine->RegisterInterfaceMethod(SU_IF_ABILITY, "void OnFinish(" SU_IF_RESULT "@)");
    engine->RegisterInterfaceMethod(SU_IF_ABILITY, "void OnJusticeCritical(" SU_IF_RESULT "@)");
    engine->RegisterInterfaceMethod(SU_IF_ABILITY, "void OnJustice(" SU_IF_RESULT "@)");
    engine->RegisterInterfaceMethod(SU_IF_ABILITY, "void OnAttack(" SU_IF_RESULT "@)");
    engine->RegisterInterfaceMethod(SU_IF_ABILITY, "void OnMiss(" SU_IF_RESULT "@)");
}

asIScriptObject* Character::LoadAbility(boost::filesystem::path spath)
{
    using namespace boost::filesystem;
    auto log = spdlog::get("main");
    auto abroot = Setting::GetRootDirectory() / SU_DATA_DIR / SU_CHARACTER_DIR / L"Abilities";
    //���������
    auto modulename = ConvertUnicodeToUTF8(spath.c_str());
    auto mod = ScriptInterface->GetExistModule(modulename);
    if (!mod) {
        ScriptInterface->StartBuildModule(modulename.c_str(), [=](wstring inc, wstring from, CWScriptBuilder *b) {
            if (!exists(abroot / inc)) return false;
            b->AddSectionFromFile((abroot / inc).wstring().c_str());
            return true;
        });
        ScriptInterface->LoadFile(spath.wstring().c_str());
        if (!ScriptInterface->FinishBuildModule()) {
            ScriptInterface->GetLastModule()->Discard();
            return nullptr;
        }
        mod = ScriptInterface->GetLastModule();
    }

    //�G���g���|�C���g����
    int cnt = mod->GetObjectTypeCount();
    asITypeInfo *type = nullptr;
    for (int i = 0; i < cnt; i++) {
        auto cti = mod->GetObjectTypeByIndex(i);
        if (!(ScriptInterface->CheckMetaData(cti, "EntryPoint") || cti->GetUserData(SU_UDTYPE_ENTRYPOINT))) continue;
        type = cti;
        type->SetUserData((void*)0xFFFFFFFF, SU_UDTYPE_ENTRYPOINT);
        type->AddRef();
        break;
    }
    if (!type) {
        log->critical(u8"�A�r���e�B�[��EntryPoint������܂���");
        return nullptr;
    }

    auto obj = ScriptInterface->InstantiateObject(type);
    obj->AddRef();
    type->Release();
    return obj;
}

Character::Character(shared_ptr<AngelScript> script, shared_ptr<Result> result, shared_ptr<CharacterInfo> info)
{
    ScriptInterface = script;
    Info = info;
    TargetResult = result;
    context = script->GetEngine()->CreateContext();
}

Character::~Character()
{
    context->Release();
    for (const auto &t : AbilityTypes) t->Release();
    for (const auto &o : Abilities) o->Release();
}

void Character::Initialize()
{
    using namespace boost::algorithm;
    using namespace boost::filesystem;
    auto log = spdlog::get("main");
    auto abroot = Setting::GetRootDirectory() / SU_DATA_DIR / SU_CHARACTER_DIR / L"Abilities";

    for (const auto &def : Info->Abilities) {
        vector<string> params;
        auto scrpath = abroot / (def.Name + ".as");

        auto abo = LoadAbility(scrpath);
        if (!abo) continue;
        auto abt = abo->GetObjectType();
        abt->AddRef();
        Abilities.push_back(abo);
        AbilityTypes.push_back(abt);

        auto init = abt->GetMethodByDecl("void Initialize(dictionary@)");
        if (!init) continue;
        
        auto at = ScriptInterface->GetEngine()->GetTypeInfoByDecl("array<string>");
        auto args = CScriptDictionary::Create(ScriptInterface->GetEngine());
        for (const auto &arg : def.Arguments) {
            auto key = arg.first;
            auto value = arg.second;
            auto &vid = value.type();
        }

        context->Prepare(init);
        context->SetObject(abo);
        context->SetArgAddress(0, args);
        context->Execute();
        log->info(u8"�A�r���e�B�[ " + ConvertUnicodeToUTF8(scrpath.c_str()));
    }
}

void Character::CallOnEvent(const char *decl)
{
    for (int i = 0; i < Abilities.size(); ++i) {
        auto func = AbilityTypes[i]->GetMethodByDecl(decl);
        if (!func) continue;
        context->Prepare(func);
        context->SetObject(Abilities[i]);
        context->SetArgAddress(0, TargetResult.get());
        context->Execute();
    }
}

void Character::OnStart()
{
    CallOnEvent("void OnStart(" SU_IF_RESULT "@)");
}

void Character::OnFinish()
{
    CallOnEvent("void OnFinish(" SU_IF_RESULT "@)");
}

void Character::OnJusticeCritical()
{
    CallOnEvent("void OnJusticeCritical(" SU_IF_RESULT "@)");
}

void Character::OnJustice()
{
    CallOnEvent("void OnJustice(" SU_IF_RESULT "@)");
}

void Character::OnAttack()
{
    CallOnEvent("void OnAttack(" SU_IF_RESULT "@)");
}

void Character::OnMiss()
{
    CallOnEvent("void OnMiss(" SU_IF_RESULT "@)");
}

shared_ptr<CharacterInfo> CharacterInfo::LoadFromToml(const boost::filesystem::path &path)
{
    auto log = spdlog::get("main");
    auto result = make_shared<CharacterInfo>();
    
    ifstream ifs(path.wstring(), ios::in);
    auto pr = toml::parse(ifs);
    ifs.close();
    
    if (!pr.valid()) return nullptr;
    auto &ci = pr.value;
    try {
        result->Name = ci.get<string>("Name");
        result->Description = ci.get<string>("Description");

        auto abilities = ci.get<vector<toml::Table>>("Abilities");
        for (const auto &ability : abilities) {
            AbilityInfo ai;
            ai.Name = ability.at("Type").as<string>();
            auto args = ability.at("Arguments").as<toml::Table>();
            for (const auto &p : args) {
                switch (p.second.type()) {
                    case toml::Value::INT_TYPE:
                        ai.Arguments[p.first] = p.second.as<int>();
                        break;
                    case toml::Value::DOUBLE_TYPE:
                        ai.Arguments[p.first] = p.second.as<double>();
                        break;
                    case toml::Value::STRING_TYPE:
                        ai.Arguments[p.first] = p.second.as<string>();
                        break;
                    default:
                        break;
                }
            }
            result->Abilities.push_back(ai);
        }

    } catch (exception) {
        log->error(u8"�L�����N�^�[ {0} �̓ǂݍ��݂Ɏ��s���܂���", ConvertUnicodeToUTF8(path.wstring()));
        return nullptr;
    }
    return result;
}
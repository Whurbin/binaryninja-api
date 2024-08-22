#include "binaryninjaapi.h"
#include "highlevelilinstruction.h"

using namespace BinaryNinja;
using namespace std;

LanguageRepresentationFunction::LanguageRepresentationFunction(Architecture* arch, Function* func, HighLevelILFunction* highLevelIL)
{
	BNCustomLanguageRepresentationFunction callbacks;
	callbacks.context = this;
	callbacks.freeObject = FreeCallback;
	callbacks.externalRefTaken = nullptr;
	callbacks.externalRefReleased = nullptr;
	callbacks.initTokenEmitter = InitTokenEmitterCallback;
	callbacks.getExprText = GetExprTextCallback;
	callbacks.beginLines = BeginLinesCallback;
	callbacks.endLines = EndLinesCallback;
	AddRefForRegistration();
	m_object = BNCreateCustomLanguageRepresentationFunction(arch->GetObject(), func->GetObject(),
		highLevelIL->GetObject(), &callbacks);
}


LanguageRepresentationFunction::LanguageRepresentationFunction(BNLanguageRepresentationFunction* func)
{
	m_object = func;
}


vector<DisassemblyTextLine> LanguageRepresentationFunction::GetExprText(const HighLevelILInstruction& instr,
	DisassemblySettings* settings, bool asFullAst, BNOperatorPrecedence precedence, bool statement)
{
	size_t count = 0;
	BNDisassemblyTextLine* lines = BNGetLanguageRepresentationFunctionExprText(m_object,
		instr.function->GetObject(), instr.exprIndex, settings ? settings->GetObject() : nullptr,
		asFullAst, precedence, statement, &count);

	vector<DisassemblyTextLine> result;
	result.reserve(count);
	for (size_t i = 0; i < count; i++)
	{
		DisassemblyTextLine line;
		line.addr = lines[i].addr;
		line.instrIndex = lines[i].instrIndex;
		line.highlight = lines[i].highlight;
		line.tokens = InstructionTextToken::ConvertInstructionTextTokenList(lines[i].tokens, lines[i].count);
		line.tags = Tag::ConvertTagList(lines[i].tags, lines[i].tagCount);
		result.push_back(line);
	}

	BNFreeDisassemblyTextLines(lines, count);
	return result;
}


vector<DisassemblyTextLine> LanguageRepresentationFunction::GetLines(
	const HighLevelILInstruction& instr, DisassemblySettings* settings, bool asFullAst)
{
	size_t count = 0;
	BNDisassemblyTextLine* lines = BNGetLanguageRepresentationFunctionLines(m_object,
		instr.function->GetObject(), instr.exprIndex, settings ? settings->GetObject() : nullptr,
		asFullAst, &count);

	vector<DisassemblyTextLine> result;
	result.reserve(count);
	for (size_t i = 0; i < count; i++)
	{
		DisassemblyTextLine line;
		line.addr = lines[i].addr;
		line.instrIndex = lines[i].instrIndex;
		line.highlight = lines[i].highlight;
		line.tokens = InstructionTextToken::ConvertInstructionTextTokenList(lines[i].tokens, lines[i].count);
		line.tags = Tag::ConvertTagList(lines[i].tags, lines[i].tagCount);
		result.push_back(line);
	}

	BNFreeDisassemblyTextLines(lines, count);
	return result;
}


vector<DisassemblyTextLine> LanguageRepresentationFunction::GetDisassemblyText(
	BasicBlock* block, DisassemblySettings* settings)
{
	size_t count = 0;
	BNDisassemblyTextLine* lines = BNGetLanguageRepresentationFunctionDisassemblyText(m_object,
		block->GetObject(), settings ? settings->GetObject() : nullptr, &count);

	vector<DisassemblyTextLine> result;
	result.reserve(count);
	for (size_t i = 0; i < count; i++)
	{
		DisassemblyTextLine line;
		line.addr = lines[i].addr;
		line.instrIndex = lines[i].instrIndex;
		line.highlight = lines[i].highlight;
		line.tokens = InstructionTextToken::ConvertInstructionTextTokenList(lines[i].tokens, lines[i].count);
		line.tags = Tag::ConvertTagList(lines[i].tags, lines[i].tagCount);
		result.push_back(line);
	}

	BNFreeDisassemblyTextLines(lines, count);
	return result;
}


BNHighlightColor LanguageRepresentationFunction::GetHighlight(BasicBlock* block)
{
	return BNGetLanguageRepresentationFunctionHighlight(m_object, block->GetObject());
}


Architecture* LanguageRepresentationFunction::GetArchitecture() const
{
	return new CoreArchitecture(BNGetLanguageRepresentationArchitecture(m_object));
}


Function* LanguageRepresentationFunction::GetFunction() const
{
	return new Function(BNGetLanguageRepresentationOwnerFunction(m_object));
}


HighLevelILFunction* LanguageRepresentationFunction::GetHighLevelILFunction() const
{
	return new HighLevelILFunction(BNGetLanguageRepresentationILFunction(m_object));
}


void LanguageRepresentationFunction::InitTokenEmitter(HighLevelILTokenEmitter&)
{
}


void LanguageRepresentationFunction::BeginLines(const HighLevelILInstruction&, HighLevelILTokenEmitter&)
{
}


void LanguageRepresentationFunction::EndLines(const HighLevelILInstruction&, HighLevelILTokenEmitter&)
{
}


void LanguageRepresentationFunction::FreeCallback(void* ctxt)
{
	LanguageRepresentationFunction* func = (LanguageRepresentationFunction*)ctxt;
	func->ReleaseForRegistration();
}


void LanguageRepresentationFunction::InitTokenEmitterCallback(void* ctxt, BNHighLevelILTokenEmitter* tokens)
{
	LanguageRepresentationFunction* func = (LanguageRepresentationFunction*)ctxt;
	Ref<HighLevelILTokenEmitter> tokenObj = new HighLevelILTokenEmitter(BNNewHighLevelILTokenEmitterReference(tokens));
	func->InitTokenEmitter(*tokenObj);
}


void LanguageRepresentationFunction::GetExprTextCallback(void* ctxt, BNHighLevelILFunction* il, size_t exprIndex,
	BNHighLevelILTokenEmitter* tokens, BNDisassemblySettings* settings, bool asFullAst,
	BNOperatorPrecedence precedence, bool statement)
{
	LanguageRepresentationFunction* func = (LanguageRepresentationFunction*)ctxt;
	Ref<HighLevelILFunction> ilObj = new HighLevelILFunction(BNNewHighLevelILFunctionReference(il));
	HighLevelILInstruction instr = ilObj->GetExpr(exprIndex);
	Ref<HighLevelILTokenEmitter> tokenObj = new HighLevelILTokenEmitter(BNNewHighLevelILTokenEmitterReference(tokens));
	Ref<DisassemblySettings> settingsObj = settings ? new DisassemblySettings(BNNewDisassemblySettingsReference(settings)) : nullptr;
	func->GetExprText(instr, *tokenObj, settingsObj, asFullAst, precedence, statement);
}


void LanguageRepresentationFunction::BeginLinesCallback(void* ctxt, BNHighLevelILFunction* il, size_t exprIndex,
	BNHighLevelILTokenEmitter* tokens)
{
	LanguageRepresentationFunction* func = (LanguageRepresentationFunction*)ctxt;
	Ref<HighLevelILFunction> ilObj = new HighLevelILFunction(BNNewHighLevelILFunctionReference(il));
	HighLevelILInstruction instr = ilObj->GetExpr(exprIndex);
	Ref<HighLevelILTokenEmitter> tokenObj = new HighLevelILTokenEmitter(BNNewHighLevelILTokenEmitterReference(tokens));
	func->BeginLines(instr, *tokenObj);
}


void LanguageRepresentationFunction::EndLinesCallback(void* ctxt, BNHighLevelILFunction* il, size_t exprIndex,
	BNHighLevelILTokenEmitter* tokens)
{
	LanguageRepresentationFunction* func = (LanguageRepresentationFunction*)ctxt;
	Ref<HighLevelILFunction> ilObj = new HighLevelILFunction(BNNewHighLevelILFunctionReference(il));
	HighLevelILInstruction instr = ilObj->GetExpr(exprIndex);
	Ref<HighLevelILTokenEmitter> tokenObj = new HighLevelILTokenEmitter(BNNewHighLevelILTokenEmitterReference(tokens));
	func->EndLines(instr, *tokenObj);
}


CoreLanguageRepresentationFunction::CoreLanguageRepresentationFunction(BNLanguageRepresentationFunction* func):
    LanguageRepresentationFunction(func)
{
}


void CoreLanguageRepresentationFunction::GetExprText(const HighLevelILInstruction&, HighLevelILTokenEmitter&,
	DisassemblySettings*, bool, BNOperatorPrecedence, bool statement)
{
}


LanguageRepresentationFunctionType::LanguageRepresentationFunctionType(const std::string& name): m_nameForRegister(name)
{
	m_object = nullptr;
}


LanguageRepresentationFunctionType::LanguageRepresentationFunctionType(BNLanguageRepresentationFunctionType* type)
{
	m_object = type;
}


string LanguageRepresentationFunctionType::GetName() const
{
	char* name = BNGetLanguageRepresentationFunctionTypeName(m_object);
	string result = name;
	BNFreeString(name);
	return result;
}


void LanguageRepresentationFunctionType::Register(LanguageRepresentationFunctionType* type)
{
	BNCustomLanguageRepresentationFunctionType callbacks;
	callbacks.context = type;
	callbacks.create = CreateCallback;

	type->AddRefForRegistration();
	type->m_object =
		BNRegisterLanguageRepresentationFunctionType(type->m_nameForRegister.c_str(), &callbacks);
}


BNLanguageRepresentationFunction* LanguageRepresentationFunctionType::CreateCallback(
	void* ctxt, BNArchitecture* arch, BNFunction* owner, BNHighLevelILFunction* highLevelIL)
{
	LanguageRepresentationFunctionType* type = (LanguageRepresentationFunctionType*)ctxt;
	Ref<Architecture> archObj = new CoreArchitecture(arch);
	Ref<Function> ownerObj = new Function(BNNewFunctionReference(owner));
	Ref<HighLevelILFunction> il = new HighLevelILFunction(BNNewHighLevelILFunctionReference(highLevelIL));
	Ref<LanguageRepresentationFunction> result = type->Create(archObj, ownerObj, il);
	if (!result)
		return nullptr;
	return BNNewLanguageRepresentationFunctionReference(result->GetObject());
}


Ref<LanguageRepresentationFunctionType> LanguageRepresentationFunctionType::GetByName(const std::string& name)
{
	BNLanguageRepresentationFunctionType* type = BNGetLanguageRepresentationFunctionTypeByName(name.c_str());
	if (!type)
		return nullptr;
    return new CoreLanguageRepresentationFunctionType(type);
}


vector<Ref<LanguageRepresentationFunctionType>> LanguageRepresentationFunctionType::GetTypes()
{
	size_t count = 0;
    BNLanguageRepresentationFunctionType** types = BNGetLanguageRepresentationFunctionTypeList(&count);

    vector<Ref<LanguageRepresentationFunctionType>> result;
    result.reserve(count);
    for (size_t i = 0; i < count; i++)
        result.push_back(new CoreLanguageRepresentationFunctionType(types[i]));

    BNFreeLanguageRepresentationFunctionTypeList(types);
    return result;
}


CoreLanguageRepresentationFunctionType::CoreLanguageRepresentationFunctionType(BNLanguageRepresentationFunctionType* type):
    LanguageRepresentationFunctionType(type)
{
}


Ref<LanguageRepresentationFunction> CoreLanguageRepresentationFunctionType::Create(
	Architecture* arch, Function* owner, HighLevelILFunction* highLevelIL)
{
	BNLanguageRepresentationFunction* func = BNCreateLanguageRepresentationFunction(
		m_object, arch->GetObject(), owner->GetObject(), highLevelIL->GetObject());
	if (!func)
		return nullptr;
	return new CoreLanguageRepresentationFunction(func);
}

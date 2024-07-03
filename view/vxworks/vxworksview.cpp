// BinaryView for VxWorks Real-Time Operating System (RTOS) images

#include "vxworksview.h"

using namespace BinaryNinja;
using namespace std;

#define MAX_SYMBOL_TABLE_REGION_SIZE 0x2000000
#define VXWORKS5_SYMBOL_ENTRY_SIZE 0x10
#define VXWORKS6_SYMBOL_ENTRY_SIZE 0x14
#define MAX_SYMBOL_NAME_LENGTH 128
#define MIN_VALID_SYMBOL_ENTRIES 1000
#define VXWORKS_SYMBOL_ENTRY_TYPE(flags) ((flags >> 8) & 0xff)
#define LOWEST_TEXT_SYMBOL_ADDRESS_START 0xffffffffffffffff
#define MAX_ADDRESSES_ENDIANNESS_CHECK 10
#define ALIGN4(x) (x & 0xfffffffffffffffc)

static const std::map<VxWorks5SymbolType, BNSymbolType> VxWorks5SymbolTypeMap = {
	{ VxWorks5UndefinedSymbolType, FunctionSymbol },
	{ VxWorks5GlobalExternalSymbolType, ImportAddressSymbol },
	{ VxWorks5LocalAbsoluteSymbolType, DataSymbol },
	{ VxWorks5GlobalAbsoluteSymbolType, DataSymbol },
	{ VxWorks5LocalTextSymbolType, FunctionSymbol },
	{ VxWorks5GlobalTextSymbolType, FunctionSymbol },
	{ VxWorks5LocalDataSymbolType, DataSymbol },
	{ VxWorks5GlobalDataSymbolType, DataSymbol },
	{ VxWorks5LocalBSSSymbolType, DataSymbol },
	{ VxWorks5GlobalBSSSymbolType, DataSymbol },
	{ VxWorks5LocalCommonSymbolType, DataSymbol },
	{ VxWorks5GlobalCommonSymbolType, DataSymbol },
	{ VxWorks5PowerPCLocalSDASymbolType, DataSymbol },
	{ VxWorks5PowerPCGlobalSDASymbolType, DataSymbol },
	{ VxWorks5PowerPCLocalSDA2SymbolType, DataSymbol },
	{ VxWorks5PowerPCGlobalSDA2SymbolType, DataSymbol },
};

static const std::map<VxWorks6SymbolType, BNSymbolType> VxWorks6SymbolTypeMap = {
	{ VxWorks6UndefinedSymbolType, FunctionSymbol },
	{ VxWorks6GlobalExternalSymbolType, ImportAddressSymbol },
	{ VxWorks6LocalAbsoluteSymbolType, DataSymbol },
	{ VxWorks6GlobalAbsoluteSymbolType, DataSymbol },
	{ VxWorks6LocalTextSymbolType, FunctionSymbol },
	{ VxWorks6GlobalTextSymbolType, FunctionSymbol },
	{ VxWorks6LocalDataSymbolType, DataSymbol },
	{ VxWorks6GlobalDataSymbolType, DataSymbol },
	{ VxWorks6LocalBSSSymbolType, DataSymbol },
	{ VxWorks6GlobalBSSSymbolType, DataSymbol },
	{ VxWorks6LocalCommonSymbolType, DataSymbol },
	{ VxWorks6GlobalCommonSymbolType, DataSymbol },
	{ VxWorks6LocalSymbols, DataSymbol },
	{ VxWorks6GlobalSymbols, DataSymbol },
	{ VxWorks6LocalSymbols, DataSymbol },
	{ VxWorks6GlobalSymbols, DataSymbol },
};

static const std::map<std::string, BNSectionSemantics> VxWorksSectionSemanticsMap = {
	{ ".text", ReadOnlyCodeSectionSemantics },
	{ ".data", ReadWriteDataSectionSemantics },
	{ ".extern", ExternalSectionSemantics },
	{ ".symtab", ReadOnlyDataSectionSemantics },
};

static VxWorksViewType* g_vxWorksViewType = nullptr;

void BinaryNinja::InitVxWorksViewType()
{
	static VxWorksViewType type;
	BinaryViewType::Register(&type);
	g_vxWorksViewType = &type;
}


VxWorksView::VxWorksView(BinaryView* data, bool parseOnly): BinaryView("VxWorks", data->GetFile(), data),
	m_parseOnly(parseOnly)
{
	CreateLogger("BinaryView");
	m_logger = CreateLogger("BinaryView.VxWorksView");
}


bool VxWorksView::IsASCIIString(std::string &s)
{
	return !std::any_of(s.begin(), s.end(), [](char c) {
        return static_cast<unsigned char>(c) > 127;
    });
}


void VxWorksView::DefineSymbolTableDataVariable()
{
	StructureBuilder symbolTableEntry;
	switch (m_version)
	{
	case VxWorksVersion5:
		symbolTableEntry.AddMember(Type::IntegerType(4, false), "unknown");
		symbolTableEntry.AddMember(Type::PointerType(m_platform->GetArchitecture(),
			Type::VoidType())->WithConfidence(BN_FULL_CONFIDENCE), "name");
		symbolTableEntry.AddMember(Type::PointerType(m_platform->GetArchitecture(),
			Type::VoidType())->WithConfidence(BN_FULL_CONFIDENCE), "address");
		symbolTableEntry.AddMember(Type::IntegerType(4, false), "flags");
		break;
	case VxWorksVersion6:
		symbolTableEntry.AddMember(Type::IntegerType(4, false), "unknown1");
		symbolTableEntry.AddMember(Type::PointerType(m_platform->GetArchitecture(),
			Type::VoidType())->WithConfidence(BN_FULL_CONFIDENCE), "name");
		symbolTableEntry.AddMember(Type::PointerType(m_platform->GetArchitecture(),
			Type::VoidType())->WithConfidence(BN_FULL_CONFIDENCE), "address");
		symbolTableEntry.AddMember(Type::IntegerType(4, false), "unknown2");
		symbolTableEntry.AddMember(Type::IntegerType(4, false), "flags");
		break;
	default:
		m_logger->LogError("VxWorks version is not set. Please report this issue.");
		return;
	}

	auto symbolEntryStruct = symbolTableEntry.Finalize();
	auto symbolEntryType = Type::StructureType(symbolEntryStruct);
	QualifiedName symbolEntryName = string("VxWorksSymbolEntry");
	auto symbolEntryTypeId = Type::GenerateAutoTypeId("VxWorks", symbolEntryName);
	QualifiedName symbolEntryTypeName = DefineType(symbolEntryTypeId, symbolEntryName, symbolEntryType);
	DefineDataVariable(m_imageBase + m_symbolTableOffset,
		Type::ArrayType(Type::NamedType(this, symbolEntryTypeName),
		m_symbols.size())
	);

	DefineAutoSymbol(new Symbol(DataSymbol, "VxWorksSymbolTable", m_imageBase + m_symbolTableOffset));
}


uint64_t VxWorksView::FindSysInit(BinaryReader *reader, uint64_t imageBase)
{
	for (const auto& entry : m_symbols)
	{
		reader->Seek(entry.name - imageBase);
		string symbolName = reader->ReadCString(MAX_SYMBOL_NAME_LENGTH);
		if (symbolName == "sysInit" || symbolName == "_sysInit")
		{
			m_logger->LogDebug("Found %s function at 0x%016x", symbolName.c_str(), entry.address);
			return entry.address;
		}
	}

	return 0;
}


// All VxWorks image headers I've seen are less than 256 bytes in size. 1k should be plenty.
#define MAX_VXWORKS_HEADER_SIZE 1024
void VxWorksView::AdjustImageBaseForHeaderIfPresent(BinaryReader *reader)
{
	// Many VxWorks images contain a header and the first dword contains the size of the header.
	reader->Seek(0);
	uint32_t possibleHeaderSz = 0;
	if (!reader->TryRead32(possibleHeaderSz))
	{
		m_logger->LogWarn("Failed to read first 4 bytes of VxWorks image. Please, report this issue.");
		return;
	}

	if (!possibleHeaderSz || possibleHeaderSz > MAX_VXWORKS_HEADER_SIZE)
		return; // Likely not a header

	// Meets size constraints, now verify that a symbol name address aligns with a known symbol name before adjusting
	if (FindSysInit(reader, m_determinedImageBase - possibleHeaderSz))
	{
		m_logger->LogDebug("sysInit found, adjusting base address by 0x%08x bytes", possibleHeaderSz);
		m_determinedImageBase -= possibleHeaderSz;
	}
}


void VxWorksView::DetermineImageBaseFromSymbols(BinaryReader* reader)
{
	// Get lowest address from symbol table text section entries
	uint64_t lowestTextAddress = LOWEST_TEXT_SYMBOL_ADDRESS_START;
	for (const auto& entry : m_symbols)
	{
		uint8_t type = VXWORKS_SYMBOL_ENTRY_TYPE(entry.flags);
		if (m_version == VxWorksVersion5 && type != VxWorks5GlobalTextSymbolType)
			continue;

		if (m_version == VxWorksVersion6 && type != VxWorks6GlobalTextSymbolType)
			continue;

		if (entry.address < lowestTextAddress)
			lowestTextAddress = entry.address;
	}

	if (lowestTextAddress == LOWEST_TEXT_SYMBOL_ADDRESS_START)
	{
		m_logger->LogWarn("Could not determine image base from VxWorks symbol table");
		return;
	}

	m_determinedImageBase = lowestTextAddress;
	AdjustImageBaseForHeaderIfPresent(reader);
	m_logger->LogDebug("Determined image base address: 0x%016x", lowestTextAddress);
	m_imageBase = m_determinedImageBase;
}


// Check least significant byte of the first 10 function symbol addresses to make sure they aren't all the same
bool VxWorksView::FunctionAddressesAreValid(VxWorksVersion version)
{
	std::vector<uint32_t> funcAddresses;
	for (const auto& entry : m_symbols)
	{
		uint8_t type = VXWORKS_SYMBOL_ENTRY_TYPE(entry.flags);
		if (version == VxWorksVersion5 && type != VxWorks5GlobalTextSymbolType)
			continue;

		if (version == VxWorksVersion6 && type != VxWorks6GlobalTextSymbolType)
			continue;

		funcAddresses.push_back(entry.address);
		if (funcAddresses.size() == MAX_ADDRESSES_ENDIANNESS_CHECK)
		{
			uint32_t val = 0;
			for (const auto& addr : funcAddresses)
				val ^= addr;
			return (val & 0xff) != 0;
		}
	}

	// Too few symbols to check
	return false;
}


bool VxWorksView::TryReadVxWorksSymbolEntry(BinaryReader *reader, uint64_t offset,
	VxWorksSymbolEntry& entry, VxWorksVersion version)
{
	reader->Seek(offset + 4); // Skip first unknown field
	if (!reader->TryRead32(entry.name) || !reader->TryRead32(entry.address))
		return false;

	if (version == VxWorksVersion6)
		reader->Seek(offset + 16); // Skip second unknown field (VxWorks 6 only)

	if (!reader->TryReadBE32(entry.flags))
		return false;

	uint8_t type = VXWORKS_SYMBOL_ENTRY_TYPE(entry.flags);
	switch (version)
	{
	case VxWorksVersion5:
	{
		auto it = VxWorks5SymbolTypeMap.find((VxWorks5SymbolType)type);
		if (entry.name != 0 && entry.flags != 0 && it != VxWorks5SymbolTypeMap.end())
			return true;

		return false;
	}
	case VxWorksVersion6:
	{
		auto it = VxWorks6SymbolTypeMap.find((VxWorks6SymbolType)type);
		if (entry.name != 0 && it != VxWorks6SymbolTypeMap.end())
			return true;

		return false;
	}
	default:
		m_logger->LogError("VxWorks version is not set. Please report this issue.");
		return false;
	}
}


bool VxWorksView::ScanForVxWorksSystemTable(BinaryReader *reader, VxWorksVersion version, BNEndianness endianness)
{
	size_t entrySize;
	m_endianness = endianness;
	reader->SetEndianness(m_endianness);
	switch (version)
	{
	case VxWorksVersion5:
		entrySize = VXWORKS5_SYMBOL_ENTRY_SIZE;
		break;
	case VxWorksVersion6:
		entrySize = VXWORKS6_SYMBOL_ENTRY_SIZE;
		break;
	default:
		m_logger->LogError("VxWorks version is not set. Please report this issue.");
		return false;
	}

	VxWorksSymbolEntry entry;
	ssize_t startOffset = ALIGN4(m_parentView->GetLength() - entrySize);
	ssize_t endOffset = 0;
	if (m_parentView->GetLength() > MAX_SYMBOL_TABLE_REGION_SIZE)
		endOffset = ALIGN4(m_parentView->GetLength() - MAX_SYMBOL_TABLE_REGION_SIZE);
	ssize_t searchPos = startOffset;
	std::vector<uint32_t> foundNames;

	m_logger->LogDebug("Scanning backwards for VxWorks system table (0x%016x-0x%016x) (endianess=%s) (version=%d)...",
		startOffset, endOffset, m_endianness == BigEndian ? "big" : "little", version == VxWorksVersion5 ? 5 : 6);
	while (searchPos > endOffset)
	{
		if (TryReadVxWorksSymbolEntry(reader, searchPos, entry, version) &&
			find(foundNames.begin(), foundNames.end(), entry.name) == foundNames.end())
		{
			foundNames.push_back(entry.address);
			m_symbols.push_back(entry);
			searchPos -= entrySize;
			continue;
		}

		if (m_symbols.size() > MIN_VALID_SYMBOL_ENTRIES && FunctionAddressesAreValid(version))
			break;

		searchPos -= 4;
		foundNames.clear();
		m_symbols.clear();
	}

	if (m_symbols.size() < MIN_VALID_SYMBOL_ENTRIES)
	{
		m_symbols.clear();
		return false;
	}

	m_symbolTableOffset = searchPos + entrySize;

	// Scan algorithm is prone to missing a few entries at the end of the table
	searchPos = m_symbolTableOffset + (entrySize * m_symbols.size());
	while (TryReadVxWorksSymbolEntry(reader, searchPos, entry, version))
	{
		m_symbols.push_back(entry);
		searchPos += entrySize;
	}

	m_version = version;
	m_logger->LogDebug("Found %d VxWorks %d symbol table entries starting at offset 0x%016x",
		m_symbols.size(), version == VxWorksVersion5 ? 5 : 6, m_symbolTableOffset);

	return true;
}


bool VxWorksView::FindSymbolTable(BinaryReader *reader)
{
	if (ScanForVxWorksSystemTable(reader, VxWorksVersion5, BigEndian))
		return true;

	if (ScanForVxWorksSystemTable(reader, VxWorksVersion5, LittleEndian))
		return true;

	if (ScanForVxWorksSystemTable(reader, VxWorksVersion6, LittleEndian))
		return true;

	return ScanForVxWorksSystemTable(reader, VxWorksVersion6, BigEndian);
}


void VxWorksView::AssignSymbolToSection(std::map<std::string, std::set<uint64_t>>& sections,
	BNSymbolType bnSymbolType, uint8_t vxSymbolType, uint64_t address)
{
	switch (bnSymbolType)
	{
	case FunctionSymbol:
		sections[".text"].insert(address);
		AddFunctionForAnalysis(m_platform, address);
		break;
	case DataSymbol:
		sections[".data"].insert(address);
		break;
	case ExternalSymbol:
		sections[".extern"].insert(address);
		break;
	default:
		m_logger->LogWarn("Unknown symbol type: %d", bnSymbolType);
		break;
	}
}


#define MAX_BAD_SYMBOLS_BEFORE_ABORT 10
void VxWorksView::ProcessSymbolTable(BinaryReader *reader)
{
	std::map<std::string, std::set<uint64_t>> sections = {
		{ ".text", {} },
		{ ".data", {} },
		{ ".extern", {} },
		{ ".symtab", {} },
	};

	size_t badSymbols = 0;
	for (const auto& entry : m_symbols)
	{
		reader->Seek(entry.name - m_imageBase);
		string symbolName = reader->ReadCString(MAX_SYMBOL_NAME_LENGTH);
		uint8_t vxSymbolType = VXWORKS_SYMBOL_ENTRY_TYPE(entry.flags);
		BNSymbolType bnSymbolType;

		switch (m_version)
		{
		case VxWorksVersion5:
		{
			auto it = VxWorks5SymbolTypeMap.find((VxWorks5SymbolType)vxSymbolType);
			if (it == VxWorks5SymbolTypeMap.end())
			{
				m_logger->LogWarn("Unknown VxWorks 5 symbol type: 0x%02x (%s)", vxSymbolType, symbolName.c_str());
				continue;
			}

			bnSymbolType = it->second;
			break;
		}
		case VxWorksVersion6:
		{
			auto it = VxWorks6SymbolTypeMap.find((VxWorks6SymbolType)vxSymbolType);
			if (it == VxWorks6SymbolTypeMap.end())
			{
				m_logger->LogWarn("Unknown VxWorks 6 symbol type: 0x%02x (%s)", vxSymbolType, symbolName.c_str());
				continue;
			}

			bnSymbolType = it->second;
			break;
		}
		default:
			m_logger->LogError("VxWorks version is not set. Please report this issue.");
			return;
		}

		// While our scanning code for symbol table discovery has been tested, false positives are possible.
		if (!IsASCIIString(symbolName) || symbolName.empty())
		{
			if (badSymbols < MAX_BAD_SYMBOLS_BEFORE_ABORT) // Hush after reporting 10 bad symbols
				m_logger->LogWarn("Symbol entry name for 0x%016x is invalid", entry.address);

			badSymbols++;
			if (badSymbols == MAX_BAD_SYMBOLS_BEFORE_ABORT)
			{
				m_logger->LogWarn(
					"%d or more symbols contain invalid names. VxWorks symbol table might not be a " \
					"symbol table at all! Please, report this issue.", MAX_BAD_SYMBOLS_BEFORE_ABORT
				);
				return;
			}

			continue;
		}

		if (m_parentView->IsOffsetBackedByFile(entry.address - m_imageBase))
		{
			AssignSymbolToSection(sections, bnSymbolType, vxSymbolType, entry.address);
			DefineAutoSymbol(new Symbol(bnSymbolType, symbolName, entry.address));
		}
	}

	// Build section info from address ranges of symbols and their types
	sections[".symtab"].insert(m_imageBase + m_symbolTableOffset);
	sections[".symtab"].insert(m_imageBase + (m_symbolTableOffset + m_symbols.size() *
		(m_version == VxWorksVersion5 ? VXWORKS5_SYMBOL_ENTRY_SIZE : VXWORKS6_SYMBOL_ENTRY_SIZE)));
	for (const auto& section : sections)
	{
		if (section.second.empty())
			continue;

		auto it = VxWorksSectionSemanticsMap.find(section.first);
		if (it == VxWorksSectionSemanticsMap.end())
		{
			m_logger->LogWarn("Unknown section semantics for section: %s. Please report this issue.",
				section.first.c_str());
			continue;
		}

		m_sections.push_back({
			{ *section.second.begin(), *section.second.rbegin() },
			section.first,
			it->second
		});
	}
}


void VxWorksView::AddSections()
{
	if (m_sections.empty())
	{
		// User overrode the base address or we couldn't find the symbol table
		m_logger->LogWarn("Creating default .text section over everything...");
		AddAutoSection(".text", m_imageBase, m_parentView->GetLength(), ReadOnlyCodeSectionSemantics);
		return;
	}

	// Sort section information by start address (section w/ highest start address first)
	m_logger->LogWarn("Creating sections from VxWorks symbol table entry ranges...");
	std::sort(m_sections.begin(), m_sections.end(), [](const VxWorksSectionInfo& a, const VxWorksSectionInfo& b)
	{
		return a.AddressRange.first > b.AddressRange.first;
	});

	uint64_t lastStart = 0;
	bool lastSection = true;
	for (const auto& section : m_sections)
	{
		uint64_t end = lastStart ? lastStart : section.AddressRange.second;
		if (lastSection)
		{
			end = m_imageBase + m_parentView->GetLength();
			lastSection = false;
		}

		m_logger->LogDebug("Creating section %s at 0x%016x-0x%016x", section.Name.c_str(),
			section.AddressRange.first, end);
		AddAutoSection(section.Name, section.AddressRange.first, end-section.AddressRange.first, section.Semantics);
		lastStart = section.AddressRange.first;
	}
}


bool VxWorksView::Init()
{
	try
	{
		m_parentView = GetParentView();
		if (!m_parentView)
		{
			m_logger->LogError("Failed to get parent view");
			return false;
		}

		BinaryReader reader(m_parentView, m_endianness);
		m_hasSymbolTable = FindSymbolTable(&reader);
		if (m_hasSymbolTable)
		{
			DetermineImageBaseFromSymbols(&reader);
			uint64_t sysInit = FindSysInit(&reader, m_determinedImageBase);
			m_entryPoint = sysInit ? sysInit : m_imageBase;
		}
		else
		{
			m_logger->LogWarn("Could not find VxWorks symbol table");
		}

		auto settings = GetLoadSettings(GetTypeName());
		if (!settings || settings->IsEmpty())
		{
			m_endianness = BinaryView::GetDefaultEndianness();
			m_addressSize = BinaryView::GetAddressSize();
			return true;
		}

		if (settings->Contains("loader.platform"))
		{
			auto platformName = settings->Get<string>("loader.platform", this);
			m_platform = Platform::GetByName(platformName);
			if (!m_platform)
			{
				m_logger->LogError("Failed to get platform");
				return false;
			}
		}

		if (settings->Contains("loader.imageBase"))
			m_imageBase = settings->Get<uint64_t>("loader.imageBase", this);

		m_arch = m_platform->GetArchitecture();
		m_addressSize = m_arch->GetAddressSize();
		SetDefaultPlatform(m_platform);
		SetDefaultArchitecture(m_arch);

		if (m_parseOnly)
			return true;

		AddAutoSegment(m_imageBase, m_parentView->GetLength(), 0, m_parentView->GetLength(),
			SegmentReadable | SegmentWritable | SegmentExecutable);

		if (m_hasSymbolTable)
		{
			if (m_determinedImageBase != m_imageBase)
				m_logger->LogWarn("VxWorks image base overriden by user. Not applying symbols...");
			else
				ProcessSymbolTable(&reader);

			DefineSymbolTableDataVariable();
		}

		AddSections();
		AddEntryPointForAnalysis(m_platform, m_entryPoint);
		return true;
	}
	catch (std::exception& e)
	{
		m_logger->LogError("Failed to parse VxWorks image: %s", e.what());
		return false;
	}

	return true;
}


uint64_t VxWorksView::PerformGetStart() const
{
	return m_imageBase;
}


uint64_t VxWorksView::PerformGetEntryPoint() const
{
	return m_entryPoint;
}


size_t VxWorksView::PerformGetAddressSize() const
{
	return m_addressSize;
}


VxWorksViewType::VxWorksViewType(): BinaryViewType("VxWorks", "VxWorks")
{
	m_logger = LogRegistry::CreateLogger("BinaryView");
}


Ref<BinaryView> VxWorksViewType::Create(BinaryView* data)
{
	try
	{
		return new VxWorksView(data);
	}
	catch (std::exception& e)
	{
		m_logger->LogError("%s<BinaryViewType> failed to create view! '%s'", GetName().c_str(), e.what());
		return nullptr;
	}
}


Ref<BinaryView> VxWorksViewType::Parse(BinaryView* data)
{
	try
	{
		return new VxWorksView(data, true);
	}
	catch (std::exception& e)
	{
		m_logger->LogError("%s<BinaryViewType> failed to parse view! '%s'", GetName().c_str(), e.what());
		return nullptr;
	}
}


bool VxWorksViewType::IsTypeValidForData(BinaryView* data)
{
	std::stringstream ss;
	ss << "{"
		"\"pattern\":\"VxWorks\","
		"\"start\":0,"
		"\"end\":" << data->GetLength()-1 << ","
		"\"raw\":false,"
		"\"ignoreCase\":false,"
		"\"overlap\":false,"
		"\"align\":1"
	"}";

	bool isValid = false;;
	auto StringSearchCallback = [&isValid](size_t index, const DataBuffer& dataBuffer) -> bool
	{
		isValid = true;
		return true;
	};

	if (!data->Search(string(ss.str()), StringSearchCallback))
		m_logger->LogWarn("Error while searching for VxWorks signatures in raw view");

	return isValid;
}


Ref<Settings> VxWorksViewType::GetLoadSettingsForData(BinaryView *data)
{
	Ref<BinaryView> viewRef = Parse(data);
	if (!viewRef || !viewRef->Init())
	{
		m_logger->LogError("View type '%s' could not be created", GetName().c_str());
		return nullptr;
	}

	// specify default load settings that can be overridden
	Ref<Settings> settings = GetDefaultLoadSettingsForData(viewRef);
	vector<string> overrides = {"loader.platform", "loader.imageBase", "loader.entryPointOffset"};
	for (const auto& override : overrides)
	{
		if (settings->Contains(override))
			settings->UpdateProperty(override, "readOnly", false);
	}

	if (!viewRef->IsRelocatable())
	{
		settings->UpdateProperty(
			"loader.imageBase", "message",
			"Base address determined from discovered VxWorks symbol table. This image is not relocatable.\n" \
			"   Overriding this value will degrade analysis and is NOT recommended."
		);
	}
	else
	{
		settings->UpdateProperty(
			"loader.imageBase", "message",
			"VxWorks symbol table was not found. Set the base address manually."
		);
	}

	return settings;
}


extern "C"
{
	BN_DECLARE_CORE_ABI_VERSION

#ifdef DEMO_VERSION
	bool VxWorksPluginInit()
#else
	BINARYNINJAPLUGIN bool CorePluginInit()
#endif
	{
		InitVxWorksViewType();
		return true;
	}
}
function manifestPath = buildAlgorithmPackage(modelName)
%BUILDALGORITHMPACKAGE Baut ein Simulink-Modell und erstellt ein Paket.
%
% Stand: 08.08.2026
%
% Das erzeugte Algorithmuspaket enthält die kompilierte Bibliothek,
% die benötigten Headerdateien und eine maschinenlesbare Beschreibung
% der Ein- und Ausgangssignale.
%
% Erzeugte Struktur:
%
%   <Modellname>.algorithm/
%       manifest.json
%       <Modellname>.dylib / .dll / .so
%       <Modellname>.h
%       <Modellname>_types.h
%       rtwtypes.h
%
% Das Manifest enthält unter anderem:
%
%   - grafischen Signalnamen
%   - Signalrichtung
%   - tatsächlichen C-Bezeichner
%   - MATLAB-/Simulink-Datentyp
%   - generierten C-Datentyp
%   - Dimensionen
%   - Elementanzahl
%   - Einheit
%   - globales Container-Symbol
%   - C-Strukturtyp des Containers
%
% Beispiel:
%
%   buildAlgorithmPackage("C_Notbremsung")
%
% Rückgabewert:
%
%   manifestPath
%       Vollständiger Pfad zur erzeugten manifest.json.

    modelName = string(modelName);

    if strlength(modelName) == 0
        error( ...
            "buildAlgorithmPackage:EmptyModelName", ...
            "Es wurde kein Modellname angegeben.");
    end

    %% Modell für Konfigurationsinformationen laden
    modelWasAlreadyLoaded = bdIsLoaded(modelName);

    if ~modelWasAlreadyLoaded
        load_system(modelName);
    end

    modelCleanup = onCleanup(@() closeModelIfOpenedHere( ...
        modelName, ...
        modelWasAlreadyLoaded)); %#ok<NASGU>

    %% Build-Konfiguration vor dem Build speichern
    systemTargetFile = string(get_param( ...
        modelName, ...
        "SystemTargetFile"));

    fprintf("Baue Modell: %s\n", modelName);

    %% Simulink-Modell bauen
    slbuild(modelName);

    %% Code Descriptor laden
    descriptor = coder.getCodeDescriptor(modelName);

    %% Ein- und Ausgänge auslesen
    inports = descriptor.getDataInterfaces("Inports");
    outports = descriptor.getDataInterfaces("Outports");

    %% Build-Verzeichnisse bestimmen
    buildDirectory = string(descriptor.BuildDir);
    buildParentDirectory = string(fileparts(buildDirectory));

    %% Paketordner erstellen
    packageDirectory = fullfile( ...
        buildParentDirectory, ...
        modelName + ".algorithm");

    if ~isfolder(packageDirectory)
        [success, message] = mkdir(packageDirectory);

        if ~success
            error( ...
                "buildAlgorithmPackage:PackageDirectoryCreationFailed", ...
                "Der Algorithmuspaket-Ordner konnte nicht erstellt " + ...
                "werden.\nPfad: %s\nGrund: %s", ...
                packageDirectory, ...
                message);
        end
    end

    %% Bibliothek suchen
    libraryPath = findGeneratedLibrary( ...
        modelName, ...
        buildDirectory);

    [~, libraryName, libraryExtension] = fileparts(libraryPath);

    libraryFileName = ...
        string(libraryName) + string(libraryExtension);

    %% Benötigte Headerdateien festlegen
    headerFiles = [
        modelName + ".h"
        modelName + "_types.h"
        "rtwtypes.h"
    ];

    %% Tatsächliches C-Speicherlayout ermitteln
    descriptorLayout = readCompiledLayout(descriptor, inports, outports);

    probeSourcePath = string(tempname) + "_algorithm_layout_probe.c";
    probeCleanup = onCleanup(@() deleteIfExisting(probeSourcePath)); %#ok<NASGU>

    generateLayoutProbeSource( ...
        modelName, ...
        descriptorLayout, ...
        probeSourcePath);

    compiledLayout = compileAndRunLayoutProbe( ...
        modelName, ...
        probeSourcePath, ...
        buildDirectory, ...
        buildParentDirectory);

    %% Manifest erzeugen
    manifest = struct;

    manifest.schemaVersion = 3;
    manifest.modelName = modelName;

    manifest.generatedAt = string(datetime( ...
        "now", ...
        "TimeZone", "local", ...
        "Format", "yyyy-MM-dd'T'HH:mm:ssXXX"));

    manifest.inputs = convertInterfaces( ...
        inports, ...
        "input", ...
        compiledLayout.inputNames, ...
        compiledLayout.inputOffsets, ...
        compiledLayout.inputByteSizes);

    manifest.outputs = convertInterfaces( ...
        outports, ...
        "output", ...
        compiledLayout.outputNames, ...
        compiledLayout.outputOffsets, ...
        compiledLayout.outputByteSizes);

    manifest.containers = struct;
    manifest.containers.input = struct( ...
        "symbol", descriptorLayout.inputContainer.symbol, ...
        "cType", descriptorLayout.inputContainer.type, ...
        "byteSize", compiledLayout.inputContainerSize);
    manifest.containers.output = struct( ...
        "symbol", descriptorLayout.outputContainer.symbol, ...
        "cType", descriptorLayout.outputContainer.type, ...
        "byteSize", compiledLayout.outputContainerSize);

    manifest.library = struct;
    manifest.library.fileName = libraryFileName;
    manifest.library.platform = getCurrentPlatform();
    manifest.library.type = "shared";
    manifest.library.entryPoints = struct( ...
        "initialize", modelName + "_initialize", ...
        "step", modelName + "_step", ...
        "terminate", modelName + "_terminate");

    manifest.abi = struct( ...
        "architecture", getCurrentArchitecture(), ...
        "endianness", compiledLayout.endianness, ...
        "pointerSize", compiledLayout.pointerSize);

    manifest.build = struct( ...
        "matlabRelease", string(version("-release")), ...
        "systemTargetFile", systemTargetFile);

    manifest.headers = headerFiles;

    %% Manifest als JSON erzeugen
    jsonText = jsonencode( ...
        manifest, ...
        PrettyPrint=true);

    manifestPath = fullfile( ...
        packageDirectory, ...
        "manifest.json");

    %% Manifest schreiben
    fileId = fopen( ...
        manifestPath, ...
        "w", ...
        "n", ...
        "UTF-8");

    if fileId == -1
        error( ...
            "buildAlgorithmPackage:ManifestWriteFailed", ...
            "Die manifest.json konnte nicht erstellt werden: %s", ...
            manifestPath);
    end

    cleanupObject = onCleanup(@() fclose(fileId));

    fprintf(fileId, "%s", jsonText);

    % Datei vor den folgenden Kopiervorgängen explizit schließen.
    clear cleanupObject

    %% Bibliothek in den Paketordner kopieren
    destinationLibraryPath = fullfile( ...
        packageDirectory, ...
        libraryFileName);

    [success, message] = copyfile( ...
        libraryPath, ...
        destinationLibraryPath, ...
        "f");

    if ~success
        error( ...
            "buildAlgorithmPackage:LibraryCopyFailed", ...
            "Die Bibliothek konnte nicht in das Algorithmuspaket " + ...
            "kopiert werden.\nQuelle: %s\nZiel: %s\nGrund: %s", ...
            libraryPath, ...
            destinationLibraryPath, ...
            message);
    end

    %% Headerdateien suchen und kopieren
    for index = 1:numel(headerFiles)

        headerName = headerFiles(index);

        headerPath = findGeneratedHeader( ...
            headerName, ...
            buildDirectory);

        destinationHeaderPath = fullfile( ...
            packageDirectory, ...
            headerName);

        [success, message] = copyfile( ...
            headerPath, ...
            destinationHeaderPath, ...
            "f");

        if ~success
            error( ...
                "buildAlgorithmPackage:HeaderCopyFailed", ...
                "Die erforderliche Headerdatei '%s' konnte nicht " + ...
                "in das Algorithmuspaket kopiert werden.\n" + ...
                "Die Datei wird benötigt, damit das Qt-Tool später " + ...
                "Ein-/Ausgangsstrukturen, Busse und Datentypen " + ...
                "erkennen kann.\nQuelle: %s\nZiel: %s\nGrund: %s", ...
                headerName, ...
                headerPath, ...
                destinationHeaderPath, ...
                message);
        end
    end

    %% Ergebnis anzeigen
    fprintf("\nAlgorithmuspaket erfolgreich erstellt:\n");
    fprintf("%s\n\n", packageDirectory);

    fprintf("Enthaltene Dateien:\n");
    fprintf("- manifest.json\n");
    fprintf("- %s\n", libraryFileName);

    for index = 1:numel(headerFiles)
        fprintf("- %s\n", headerFiles(index));
    end
end


function result = convertInterfaces(interfaces, direction, layoutNames, layoutOffsets, layoutByteSizes)
%CONVERTINTERFACES Wandelt Code-Descriptor-Interfaces in Manifestdaten um.
%
% Stand: 25.07.2026
%
% Neben dem grafischen Signalnamen werden auch Informationen zur
% tatsächlichen Implementierung im generierten C-Code gespeichert.
%
% Erzeugte Felder:
%
%   name
%   direction
%   codeIdentifier
%   dataType
%   cType
%   dimensions
%   elementCount
%   unit
%   containerSymbol
%   containerType

    direction = string(direction);

    if direction ~= "input" && direction ~= "output"
        error( ...
            "buildAlgorithmPackage:InvalidSignalDirection", ...
            "Ungültige Signalrichtung '%s'. Erwartet wurde " + ...
            "'input' oder 'output'.", ...
            direction);
    end

    template = struct( ...
        "name", "", ...
        "direction", "", ...
        "codeIdentifier", "", ...
        "dataType", "", ...
        "cType", "", ...
        "dimensions", [], ...
        "elementCount", 0, ...
        "unit", "", ...
        "containerSymbol", "", ...
        "containerType", "", ...
        "byteOffset", 0, ...
        "byteSize", 0);

    result = repmat( ...
        template, ...
        1, ...
        numel(interfaces));

    for index = 1:numel(interfaces)

        interfaceInfo = interfaces(index);
        typeInfo = interfaceInfo.Type;

        [dataType, cType, dimensions] = ...
            convertTypeInformation(typeInfo);

        implementationInfo = ...
            getImplementationInformation(interfaceInfo);

        layoutIndex = find( ...
            layoutNames == implementationInfo.codeIdentifier, ...
            1);

        if isempty(layoutIndex)
            error( ...
                "buildAlgorithmPackage:LayoutFieldNotFound", ...
                "Für das C-Feld '%s' wurden keine Layoutdaten gefunden.", ...
                implementationInfo.codeIdentifier);
        end

        result(index).name = ...
            string(interfaceInfo.GraphicalName);

        result(index).direction = direction;

        result(index).codeIdentifier = ...
            implementationInfo.codeIdentifier;

        result(index).dataType = dataType;
        result(index).cType = cType;
        result(index).dimensions = dimensions;

        result(index).elementCount = ...
            prod(double(dimensions));

        result(index).unit = ...
            getInterfaceUnit(interfaceInfo);

        result(index).containerSymbol = ...
            implementationInfo.containerSymbol;

        result(index).containerType = ...
            implementationInfo.containerType;

        result(index).byteOffset = ...
            layoutOffsets(layoutIndex);

        result(index).byteSize = ...
            layoutByteSizes(layoutIndex);
    end
end


function [dataType, cType, dimensions] = ...
    convertTypeInformation(typeInfo)
%CONVERTTYPEINFORMATION Liest Datentyp und Dimensionen aus.
%
% Stand: 25.07.2026
%
% Matrix-Signale besitzen einen Basistyp und Dimensionen. Skalare
% Signale werden im Manifest einheitlich mit [1, 1] beschrieben.

    if isempty(typeInfo)
        error( ...
            "buildAlgorithmPackage:MissingTypeInformation", ...
            "Für ein Signal wurden keine Typinformationen gefunden.");
    end

    if isa(typeInfo, "coder.descriptor.types.Matrix")

        baseType = typeInfo.BaseType;

        dataType = string(baseType.Name);
        cType = string(baseType.Identifier);

        dimensions = reshape( ...
            double(typeInfo.Dimensions), ...
            1, ...
            []);

    else

        dataType = string(typeInfo.Name);
        cType = string(typeInfo.Identifier);
        dimensions = [1, 1];

    end

    if isempty(dimensions)
        dimensions = [1, 1];
    end

    if any(dimensions <= 0)
        error( ...
            "buildAlgorithmPackage:InvalidDimensions", ...
            "Es wurden ungültige Signaldimensionen erkannt.");
    end
end


function unit = getInterfaceUnit(interfaceInfo)
%GETINTERFACEUNIT Liest die Einheit eines Signals aus.
%
% Stand: 25.07.2026
%
% Ist keine Einheit verfügbar, wird ein leerer Text gespeichert.

    unit = "";

    if isprop(interfaceInfo, "Unit")

        interfaceUnit = interfaceInfo.Unit;

        if ~isempty(interfaceUnit)
            unit = string(interfaceUnit);
        end
    end

    if ismissing(unit)
        unit = "";
    end
end


function result = getImplementationInformation(interfaceInfo)
%GETIMPLEMENTATIONINFORMATION Liest die reale C-Implementierung aus.
%
% Stand: 25.07.2026
%
% Typischer Fall bei einem mit ert_shrlib erzeugten Modell:
%
%   c_wandfolgen_U.lidar_x
%
% Dabei ist:
%
%   containerSymbol = "c_wandfolgen_U"
%   codeIdentifier  = "lidar_x"
%   containerType   = "ExtU_c_wandfolgen_T"
%
% Auch separat exportierte globale Variablen werden berücksichtigt.

    result = struct( ...
        "codeIdentifier", "", ...
        "containerSymbol", "", ...
        "containerType", "");

    implementation = interfaceInfo.Implementation;

    if isempty(implementation)
        error( ...
            "buildAlgorithmPackage:MissingImplementation", ...
            "Für das Signal '%s' enthält der Code Descriptor " + ...
            "keine Implementierungsinformationen.", ...
            string(interfaceInfo.GraphicalName));
    end

    implementationClass = string(class(implementation));

    %% Tatsächlichen C-Bezeichner bestimmen
    if isprop(implementation, "ElementIdentifier")

        result.codeIdentifier = ...
            string(implementation.ElementIdentifier);

    elseif isprop(implementation, "Identifier")

        result.codeIdentifier = ...
            string(implementation.Identifier);

    else

        error( ...
            "buildAlgorithmPackage:UnknownImplementation", ...
            "Der C-Bezeichner des Signals '%s' konnte nicht " + ...
            "aus der Implementierung '%s' bestimmt werden.", ...
            string(interfaceInfo.GraphicalName), ...
            implementationClass);
    end

    %% Container beziehungsweise globale Variable bestimmen
    if isprop(implementation, "BaseRegion")

        baseRegion = implementation.BaseRegion;

        if isempty(baseRegion)
            error( ...
                "buildAlgorithmPackage:MissingBaseRegion", ...
                "Für das Signal '%s' wurde keine BaseRegion gefunden.", ...
                string(interfaceInfo.GraphicalName));
        end

        if isprop(baseRegion, "Identifier") && ...
                ~isempty(baseRegion.Identifier)

            result.containerSymbol = ...
                string(baseRegion.Identifier);
        end

        if isprop(baseRegion, "Type") && ...
                ~isempty(baseRegion.Type) && ...
                isprop(baseRegion.Type, "Identifier") && ...
                ~isempty(baseRegion.Type.Identifier)

            result.containerType = ...
                string(baseRegion.Type.Identifier);
        end

    else

        % Separat exportierte globale Variable:
        %
        % In diesem Fall ist das Signal selbst das globale Symbol.
        result.containerSymbol = result.codeIdentifier;

        if isprop(implementation, "Type") && ...
                ~isempty(implementation.Type) && ...
                isprop(implementation.Type, "Identifier") && ...
                ~isempty(implementation.Type.Identifier)

            result.containerType = ...
                string(implementation.Type.Identifier);
        end
    end

    %% Ergebnis validieren
    if ismissing(result.codeIdentifier) || ...
            strlength(result.codeIdentifier) == 0

        error( ...
            "buildAlgorithmPackage:EmptyCodeIdentifier", ...
            "Für das Signal '%s' wurde kein C-Bezeichner gefunden.", ...
            string(interfaceInfo.GraphicalName));
    end

    if ismissing(result.containerSymbol) || ...
            strlength(result.containerSymbol) == 0

        error( ...
            "buildAlgorithmPackage:EmptyContainerSymbol", ...
            "Für das Signal '%s' wurde kein globales C-Symbol gefunden.", ...
            string(interfaceInfo.GraphicalName));
    end
end


function libraryPath = findGeneratedLibrary( ...
    modelName, ...
    buildDirectory)
%FINDGENERATEDLIBRARY Sucht die erzeugte native Bibliothek.
%
% Stand: 25.07.2026
%
% Unterstützte Bibliotheksformate:
%
%   macOS:   <Modellname>.dylib
%   Windows: <Modellname>.dll
%   Linux:   <Modellname>.so oder lib<Modellname>.so

    modelName = string(modelName);
    buildDirectory = string(buildDirectory);
    parentDirectory = string(fileparts(buildDirectory));

    if ismac

        libraryNames = modelName + ".dylib";

    elseif ispc

        % ert_shrlib erzeugt unter Windows typischerweise einen
        % plattformspezifischen Namen, z. B.
        %
        %   C_Notbremsung_win64.dll
        %
        % Der Name ohne Architektursuffix wird zusätzlich als
        % Fallback unterstützt.
        matlabArchitecture = string(computer("arch"));

        libraryNames = [
            modelName + "_" + matlabArchitecture + ".dll"
            modelName + ".dll"
        ];

    else

        libraryNames = [
            modelName + ".so"
            "lib" + modelName + ".so"
        ];

    end

    searchDirectories = [
        parentDirectory
        buildDirectory
    ];

    libraryPath = "";

    %% Zuerst direkt in den erwarteten Verzeichnissen suchen
    for directoryIndex = 1:numel(searchDirectories)

        for nameIndex = 1:numel(libraryNames)

            candidatePath = fullfile( ...
                searchDirectories(directoryIndex), ...
                libraryNames(nameIndex));

            if isfile(candidatePath)
                libraryPath = candidatePath;
                return;
            end
        end
    end

    %% Danach rekursiv suchen
    for directoryIndex = 1:numel(searchDirectories)

        for nameIndex = 1:numel(libraryNames)

            matches = dir(fullfile( ...
                searchDirectories(directoryIndex), ...
                "**", ...
                libraryNames(nameIndex)));

            matches = matches(~[matches.isdir]);

            if ~isempty(matches)
                libraryPath = string(fullfile( ...
                    matches(1).folder, ...
                    matches(1).name));
                return;
            end
        end
    end

    error( ...
        "buildAlgorithmPackage:LibraryNotFound", ...
        "Für das Modell '%s' wurde keine erzeugte Bibliothek " + ...
        "gefunden. Das Modell muss mit einem Shared-Library-Target " + ...
        "wie 'ert_shrlib.tlc' gebaut werden.", ...
        modelName);
end


function headerPath = findGeneratedHeader( ...
    headerName, ...
    buildDirectory)
%FINDGENERATEDHEADER Sucht eine erforderliche Headerdatei.
%
% Stand: 25.07.2026
%
% Zuerst wird direkt im Build-Ordner und dessen übergeordnetem
% Verzeichnis gesucht. Anschließend erfolgt eine rekursive Suche.

    headerName = string(headerName);
    buildDirectory = string(buildDirectory);
    parentDirectory = string(fileparts(buildDirectory));

    searchDirectories = [
        buildDirectory
        parentDirectory
    ];

    headerPath = "";

    %% Zuerst direkt in den erwarteten Verzeichnissen suchen
    for directoryIndex = 1:numel(searchDirectories)

        candidatePath = fullfile( ...
            searchDirectories(directoryIndex), ...
            headerName);

        if isfile(candidatePath)
            headerPath = candidatePath;
            return;
        end
    end

    %% Danach rekursiv in den Build-Verzeichnissen suchen
    for directoryIndex = 1:numel(searchDirectories)

        matches = dir(fullfile( ...
            searchDirectories(directoryIndex), ...
            "**", ...
            headerName));

        matches = matches(~[matches.isdir]);

        if ~isempty(matches)
            headerPath = string(fullfile( ...
                matches(1).folder, ...
                matches(1).name));
            return;
        end
    end

    error( ...
        "buildAlgorithmPackage:RequiredHeaderNotFound", ...
        "Die erforderliche Headerdatei '%s' wurde nicht gefunden.\n" + ...
        "Das Algorithmuspaket wird nicht erstellt, weil diese Datei " + ...
        "für die spätere Erkennung der generierten C-Strukturen, " + ...
        "Busse und Datentypen im Qt-Tool benötigt wird.\n" + ...
        "Geprüft wurden der Build-Ordner und dessen übergeordnetes " + ...
        "Verzeichnis:\n%s\n%s", ...
        headerName, ...
        buildDirectory, ...
        parentDirectory);
end


function platformName = getCurrentPlatform()
%GETCURRENTPLATFORM Liefert die aktuelle Plattformbezeichnung.
%
% Stand: 25.07.2026

    if ismac

        platformName = "macOS";

    elseif ispc

        platformName = "Windows";

    elseif isunix

        platformName = "Linux";

    else

        platformName = "Unknown";

    end
end


function architecture = getCurrentArchitecture()
%GETCURRENTARCHITECTURE Liefert eine portable Architekturbezeichnung.

    matlabArchitecture = string(computer("arch"));

    switch matlabArchitecture
        case "maca64"
            architecture = "arm64";
        case "maci64"
            architecture = "x86_64";
        case {"win64", "glnxa64"}
            architecture = "x86_64";
        otherwise
            architecture = matlabArchitecture;
    end
end


function layout = readCompiledLayout(descriptor, inports, outports)
%READCOMPILEDLAYOUT Liest Container, Strukturtypen und Feldnamen aus.

    layout = struct;
    layout.inputContainer = readLayoutContainer(inports, "input");
    layout.outputContainer = readLayoutContainer(outports, "output");

    fprintf("\nInput-Container:\n");
    printLayoutContainer(layout.inputContainer);

    fprintf("\nOutput-Container:\n");
    printLayoutContainer(layout.outputContainer);

    %#ok<NASGU> descriptor wird bewusst als Parameter übergeben, damit
    % die Funktion eindeutig zum bereits geladenen Build gehört.
end


function container = readLayoutContainer(interfaces, direction)
%READLAYOUTCONTAINER Liest einen Ein- oder Ausgangscontainer aus.

    container = struct( ...
        "direction", string(direction), ...
        "symbol", "", ...
        "type", "", ...
        "fields", strings(0, 1));

    if isempty(interfaces)
        return;
    end

    firstImplementation = interfaces(1).Implementation;

    if ~isprop(firstImplementation, "BaseRegion") || ...
            isempty(firstImplementation.BaseRegion)
        error( ...
            "buildAlgorithmPackage:UnsupportedLayout", ...
            "Die %s-Signale liegen nicht in einer gemeinsamen C-Struktur.", ...
            direction);
    end

    baseRegion = firstImplementation.BaseRegion;
    container.symbol = string(baseRegion.Identifier);
    container.type = string(baseRegion.Type.Identifier);
    container.fields = strings(numel(interfaces), 1);

    for index = 1:numel(interfaces)
        implementation = interfaces(index).Implementation;

        if ~isprop(implementation, "BaseRegion") || ...
                isempty(implementation.BaseRegion) || ...
                string(implementation.BaseRegion.Identifier) ~= container.symbol
            error( ...
                "buildAlgorithmPackage:MultipleContainers", ...
                "Die %s-Signale liegen in mehreren C-Containern.", ...
                direction);
        end

        container.fields(index) = ...
            string(implementation.ElementIdentifier);
    end
end


function printLayoutContainer(container)
%PRINTLAYOUTCONTAINER Gibt Descriptor-Layoutinformationen aus.

    if strlength(container.symbol) == 0
        fprintf("  Keine Signale vorhanden.\n");
        return;
    end

    fprintf("  Symbol: %s\n", container.symbol);
    fprintf("  Typ:    %s\n", container.type);

    for index = 1:numel(container.fields)
        fprintf("  Feld:   %s\n", container.fields(index));
    end
end


function sourcePath = generateLayoutProbeSource(modelName, layout, sourcePath)
%GENERATELAYOUTPROBESOURCE Erzeugt ein temporäres C-Prüfprogramm.

    modelName = string(modelName);
    sourcePath = string(sourcePath);

    inputContainer = layout.inputContainer;
    outputContainer = layout.outputContainer;

    lines = strings(0, 1);
    lines(end + 1) = "#include <stdio.h>";
    lines(end + 1) = "#include <stddef.h>";
    lines(end + 1) = '#include "' + modelName + '.h"';
    lines(end + 1) = "";
    lines(end + 1) = "int main(void)";
    lines(end + 1) = "{";
    lines(end + 1) = ...
        '    const unsigned int endianTest = 1U;';
    lines(end + 1) = sprintf( ...
        '    printf("ABI_POINTER_SIZE;%%llu\\n", (unsigned long long)sizeof(void *));');
    lines(end + 1) = ...
        '    printf("ABI_ENDIANNESS;%s\n", (*(const unsigned char *)&endianTest == 1U) ? "little" : "big");';

    if strlength(inputContainer.type) > 0
        lines(end + 1) = sprintf( ...
            '    printf("INPUT_CONTAINER_SIZE;%%llu\\n", (unsigned long long)sizeof(%s));', ...
            char(inputContainer.type));

        for index = 1:numel(inputContainer.fields)
            fieldName = inputContainer.fields(index);
            lines(end + 1) = sprintf( ...
                ['    printf("INPUT;%s;%%llu;%%llu\\n", ' ...
                 '(unsigned long long)offsetof(%s, %s), ' ...
                 '(unsigned long long)sizeof(((%s *)0)->%s));'], ...
                char(fieldName), ...
                char(inputContainer.type), ...
                char(fieldName), ...
                char(inputContainer.type), ...
                char(fieldName));
        end
    else
        lines(end + 1) = '    printf("INPUT_CONTAINER_SIZE;0\\n");';
    end

    if strlength(outputContainer.type) > 0
        lines(end + 1) = sprintf( ...
            '    printf("OUTPUT_CONTAINER_SIZE;%%llu\\n", (unsigned long long)sizeof(%s));', ...
            char(outputContainer.type));

        for index = 1:numel(outputContainer.fields)
            fieldName = outputContainer.fields(index);
            lines(end + 1) = sprintf( ...
                ['    printf("OUTPUT;%s;%%llu;%%llu\\n", ' ...
                 '(unsigned long long)offsetof(%s, %s), ' ...
                 '(unsigned long long)sizeof(((%s *)0)->%s));'], ...
                char(fieldName), ...
                char(outputContainer.type), ...
                char(fieldName), ...
                char(outputContainer.type), ...
                char(fieldName));
        end
    else
        lines(end + 1) = '    printf("OUTPUT_CONTAINER_SIZE;0\\n");';
    end

    lines(end + 1) = "    return 0;";
    lines(end + 1) = "}";

    fileId = fopen(char(sourcePath), "w");

    if fileId == -1
        error( ...
            "buildAlgorithmPackage:ProbeWriteFailed", ...
            "Die temporäre Layout-Prüfdatei konnte nicht erstellt werden: %s", ...
            sourcePath);
    end

    cleanupObject = onCleanup(@() fclose(fileId)); %#ok<NASGU>
    fprintf(fileId, "%s\n", char(strjoin(lines, newline)));
end

function compiledLayout = compileAndRunLayoutProbe( ...
    modelName, sourcePath, buildDirectory, buildParentDirectory)
%COMPILEANDRUNLAYOUTPROBE Kompiliert und startet das C-Prüfprogramm.
%
% Unterstützte Plattformen:
%
%   macOS:
%       Apple Clang über xcrun
%
%   Windows:
%       Microsoft Visual C++ (cl.exe)
%
% Das Prüfprogramm verwendet exakt die vom generierten Modell verwendeten
% C-Strukturen und ermittelt dadurch deren tatsächliches Speicherlayout.

    modelName = string(modelName);
    sourcePath = string(sourcePath);
    buildDirectory = string(buildDirectory);
    buildParentDirectory = string(buildParentDirectory);

    %% Generierten Modell-Header finden
    modelHeaderPath = findGeneratedHeader( ...
        modelName + ".h", ...
        buildDirectory);

    fprintf("\nVerwendeter Modell-Header:\n%s\n", modelHeaderPath);

    %% Benötigte Include-Verzeichnisse sammeln
    includeDirectories = collectIncludeDirectories( ...
        buildDirectory, ...
        buildParentDirectory, ...
        string(fileparts(modelHeaderPath)));

    %% Plattformabhängig kompilieren
    if ismac

        %% -------------------------------------------------------------
        %  macOS: Apple Clang
        % --------------------------------------------------------------

        [sdkStatus, sdkOutput] = system( ...
            "xcrun --sdk macosx --show-sdk-path");

        sdkPath = strtrim(string(sdkOutput));

        if sdkStatus ~= 0 || strlength(sdkPath) == 0
            error( ...
                "buildAlgorithmPackage:SdkNotFound", ...
                "Das macOS SDK konnte über xcrun nicht gefunden werden.");
        end

        [clangStatus, clangOutput] = system( ...
            "xcrun --sdk macosx --find clang");

        clangPath = strtrim(string(clangOutput));

        if clangStatus ~= 0 || strlength(clangPath) == 0
            error( ...
                "buildAlgorithmPackage:ClangNotFound", ...
                "Apple Clang konnte über xcrun nicht gefunden werden.");
        end

        fprintf("\nC-Compiler:\n%s\n", clangPath);
        fprintf("\nmacOS SDK:\n%s\n", sdkPath);

        executablePath = ...
            string(tempname) + "_layout_probe";

        executableCleanup = onCleanup( ...
            @() deleteIfExisting(executablePath)); %#ok<NASGU>

        commandParts = strings(0, 1);

        commandParts(end + 1) = shellQuote(clangPath);
        commandParts(end + 1) = "-std=c11";
        commandParts(end + 1) = "-Wall";
        commandParts(end + 1) = "-Wextra";
        commandParts(end + 1) = "-isysroot";
        commandParts(end + 1) = shellQuote(sdkPath);

        for index = 1:numel(includeDirectories)

            commandParts(end + 1) = ...
                "-I" + shellQuote(includeDirectories(index));

        end

        commandParts(end + 1) = shellQuote(sourcePath);
        commandParts(end + 1) = "-o";
        commandParts(end + 1) = shellQuote(executablePath);

        compileCommand = strjoin(commandParts, " ");

        fprintf("\nLayout-Prüfprogramm wird kompiliert ...\n");

        [compileStatus, compileOutput] = ...
            system(compileCommand);

        if compileStatus ~= 0
            error( ...
                "buildAlgorithmPackage:ProbeCompilationFailed", ...
                ["Die Layout-Prüfung konnte nicht kompiliert werden.\n\n" ...
                 "Compiler-Ausgabe:\n%s\n\nCompilerbefehl:\n%s"], ...
                compileOutput, ...
                compileCommand);
        end

        fprintf("Kompilierung erfolgreich.\n");
        fprintf("\nLayout-Prüfprogramm wird ausgeführt ...\n");

        [runStatus, runOutput] = ...
            system(shellQuote(executablePath));


    elseif ispc

        %% -------------------------------------------------------------
        %  Windows: Microsoft Visual C++
        % --------------------------------------------------------------

        executablePath = ...
            string(tempname) + "_layout_probe.exe";

        objectPath = ...
            string(tempname) + "_layout_probe.obj";

        executableCleanup = onCleanup( ...
            @() deleteIfExisting(executablePath)); %#ok<NASGU>

        objectCleanup = onCleanup( ...
            @() deleteIfExisting(objectPath)); %#ok<NASGU>

        %% Prüfen, ob cl.exe bereits verfügbar ist
        [compilerStatus, compilerOutput] = ...
            system("where cl");

        compilerAvailable = ...
            compilerStatus == 0 && ...
            strlength(strtrim(string(compilerOutput))) > 0;

        setupScript = fullfile( ...
            buildDirectory, ...
            "setup_msvc.bat");

        if compilerAvailable

            compilerPaths = splitlines( ...
                strtrim(string(compilerOutput)));

            compilerPath = compilerPaths(1);

            fprintf("\nC-Compiler:\n%s\n", compilerPath);

        elseif isfile(setupScript)

            % Simulink erzeugt für den verwendeten MSVC-Toolchain-Build
            % ein Setup-Skript. Dieses kann verwendet werden, wenn
            % cl.exe nicht direkt im MATLAB-Prozesspfad verfügbar ist.
            fprintf( ...
                "\nMSVC-Umgebung wird über folgendes Skript geladen:\n%s\n", ...
                setupScript);

        else

            error( ...
                "buildAlgorithmPackage:MsvcNotFound", ...
                ["Microsoft Visual C++ wurde zwar für den Simulink-Build " ...
                 "verwendet, cl.exe ist für die Layout-Prüfung jedoch " ...
                 "nicht verfügbar.\n\n" ...
                 "Bitte prüfen Sie die MATLAB-Compilerkonfiguration mit:\n" ...
                 "mex -setup C"]);
        end

        %% cl-Befehl aufbauen
        commandParts = strings(0, 1);

        commandParts(end + 1) = "cl";
        commandParts(end + 1) = "/nologo";

        % Quelldatei explizit als C und nicht als C++ behandeln.
        commandParts(end + 1) = "/TC";

        % Hohe Warnstufe; Warnungen bleiben erlaubt.
        commandParts(end + 1) = "/W4";

        for index = 1:numel(includeDirectories)

            commandParts(end + 1) = ...
                "/I" + windowsCmdQuote(includeDirectories(index));

        end

        commandParts(end + 1) = ...
            windowsCmdQuote(sourcePath);

        commandParts(end + 1) = ...
            "/Fo" + windowsCmdQuote(objectPath);

        commandParts(end + 1) = ...
            "/Fe" + windowsCmdQuote(executablePath);

        baseCompileCommand = ...
            strjoin(commandParts, " ");

        %% Falls cl.exe nicht direkt verfügbar ist, zuerst MSVC-Setup laden
        if compilerAvailable

            compileCommand = baseCompileCommand;

        else

            compileCommand = ...
                "call " + windowsCmdQuote(setupScript) + ...
                " && " + baseCompileCommand;

        end

        fprintf("\nLayout-Prüfprogramm wird kompiliert ...\n");

        [compileStatus, compileOutput] = ...
            system(compileCommand);

        if compileStatus ~= 0
            error( ...
                "buildAlgorithmPackage:ProbeCompilationFailed", ...
                ["Die Layout-Prüfung konnte unter Windows nicht " ...
                 "kompiliert werden.\n\n" ...
                 "Compiler-Ausgabe:\n%s\n\nCompilerbefehl:\n%s"], ...
                compileOutput, ...
                compileCommand);
        end

        fprintf("Kompilierung erfolgreich.\n");
        fprintf("\nLayout-Prüfprogramm wird ausgeführt ...\n");

        [runStatus, runOutput] = ...
            system(windowsCmdQuote(executablePath));


    else

        error( ...
            "buildAlgorithmPackage:UnsupportedProbePlatform", ...
            "Die integrierte Layout-Ermittlung unterstützt derzeit macOS und Windows.");
    end

    %% Ausführung prüfen
    if runStatus ~= 0
        error( ...
            "buildAlgorithmPackage:ProbeExecutionFailed", ...
            "Das Layout-Prüfprogramm ist fehlgeschlagen:\n%s", ...
            runOutput);
    end

    %% Ausgabe auswerten
    compiledLayout = ...
        parseLayoutProbeOutput(string(runOutput));

    validateCompiledLayout(compiledLayout);

    fprintf("\nErmitteltes Speicherlayout:\n");
    disp(compiledLayout);
end

function includeDirectories = collectIncludeDirectories( ...
    buildDirectory, buildParentDirectory, modelHeaderDirectory)
%COLLECTINCLUDEDIRECTORIES Sammelt nur buildbezogene Include-Ordner.

    roots = unique([ ...
        string(buildDirectory)
        string(buildParentDirectory)
        string(modelHeaderDirectory)], ...
        "stable");

    includeDirectories = roots;

    for rootIndex = 1:numel(roots)
        if ~isfolder(roots(rootIndex))
            continue;
        end

        headerFiles = dir(fullfile(roots(rootIndex), "**", "*.h"));

        for fileIndex = 1:numel(headerFiles)
            includeDirectories(end + 1, 1) = ...
                string(headerFiles(fileIndex).folder); %#ok<AGROW>
        end
    end

    includeDirectories = unique(includeDirectories, "stable");
end


function compiledLayout = parseLayoutProbeOutput(output)
%PARSELAYOUTPROBEOUTPUT Wandelt die Textausgabe in eine Struktur um.

    compiledLayout = struct;
    compiledLayout.pointerSize = 0;
    compiledLayout.endianness = "";
    compiledLayout.inputContainerSize = 0;
    compiledLayout.outputContainerSize = 0;
    compiledLayout.inputNames = strings(0, 1);
    compiledLayout.inputOffsets = zeros(1, 0);
    compiledLayout.inputByteSizes = zeros(1, 0);
    compiledLayout.outputNames = strings(0, 1);
    compiledLayout.outputOffsets = zeros(1, 0);
    compiledLayout.outputByteSizes = zeros(1, 0);

    lines = splitlines(output);

    for index = 1:numel(lines)
        line = strtrim(lines(index));

        if strlength(line) == 0
            continue;
        end

        parts = split(line, ";");
        recordType = parts(1);

        switch recordType
            case "ABI_POINTER_SIZE"
                compiledLayout.pointerSize = str2double(parts(2));

            case "ABI_ENDIANNESS"
                compiledLayout.endianness = string(parts(2));

            case "INPUT_CONTAINER_SIZE"
                compiledLayout.inputContainerSize = str2double(parts(2));

            case "OUTPUT_CONTAINER_SIZE"
                compiledLayout.outputContainerSize = str2double(parts(2));

            case "INPUT"
                compiledLayout.inputNames(end + 1, 1) = parts(2);
                compiledLayout.inputOffsets(end + 1) = str2double(parts(3));
                compiledLayout.inputByteSizes(end + 1) = str2double(parts(4));

            case "OUTPUT"
                compiledLayout.outputNames(end + 1, 1) = parts(2);
                compiledLayout.outputOffsets(end + 1) = str2double(parts(3));
                compiledLayout.outputByteSizes(end + 1) = str2double(parts(4));

            otherwise
                error( ...
                    "buildAlgorithmPackage:UnexpectedProbeOutput", ...
                    "Unbekannte Ausgabezeile des Layout-Prüfprogramms: %s", ...
                    line);
        end
    end
end


function validateCompiledLayout(layout)
%VALIDATECOMPILEDLAYOUT Prüft die vom C-Compiler gelieferten Werte.

    numericValues = [ ...
        layout.pointerSize, ...
        layout.inputContainerSize, ...
        layout.outputContainerSize, ...
        layout.inputOffsets, ...
        layout.inputByteSizes, ...
        layout.outputOffsets, ...
        layout.outputByteSizes];

    if any(~isfinite(numericValues)) || any(numericValues < 0)
        error( ...
            "buildAlgorithmPackage:InvalidCompiledLayout", ...
            "Die Layout-Prüfung hat ungültige Bytewerte geliefert.");
    end

    if layout.pointerSize <= 0 || ...
            ~(layout.endianness == "little" || layout.endianness == "big")
        error( ...
            "buildAlgorithmPackage:InvalidAbiInformation", ...
            "Die Layout-Prüfung hat ungültige ABI-Informationen geliefert.");
    end

    if numel(layout.inputNames) ~= numel(layout.inputOffsets) || ...
            numel(layout.inputNames) ~= numel(layout.inputByteSizes) || ...
            numel(layout.outputNames) ~= numel(layout.outputOffsets) || ...
            numel(layout.outputNames) ~= numel(layout.outputByteSizes)
        error( ...
            "buildAlgorithmPackage:IncompleteCompiledLayout", ...
            "Die Layout-Prüfung hat unvollständige Felddaten geliefert.");
    end
end


function quotedValue = shellQuote(value)
%SHELLQUOTE Maskiert einen Wert für die Unix-/macOS-Shell.
%
% Die Pfade werden in doppelte Anführungszeichen gesetzt. Enthaltene
% Backslashes, doppelte Anführungszeichen, Dollarzeichen und Backticks
% werden geschützt.

    value = char(string(value));
    value = strrep(value, '\', '\\');
    value = strrep(value, '"', '\"');
    value = strrep(value, '$', '\$');
    value = strrep(value, '`', '\`');
    quotedValue = '"' + string(value) + '"';
end

function quotedValue = windowsCmdQuote(value)
%WINDOWSCMDQUOTE Setzt einen Pfad sicher in Anführungszeichen für cmd.exe.
%
% Windows-Pfade dürfen ihre Backslashes unverändert behalten.

value = string(value);

if contains(value, '"')
    error( ...
        "buildAlgorithmPackage:InvalidWindowsPath", ...
        "Ein Windows-Pfad enthält ein doppeltes Anführungszeichen: %s", ...
        value);
end

quotedValue = '"' + value + '"';
end

function deleteIfExisting(filePath)
%DELETEIFEXISTING Löscht eine temporäre Datei.

    if isfile(filePath)
        delete(filePath);
    end
end

function closeModelIfOpenedHere(modelName, modelWasAlreadyLoaded)
%CLOSEMODELIFOPENEDHERE Schließt nur Modelle, die dieses Skript geladen hat.

if ~modelWasAlreadyLoaded && bdIsLoaded(modelName)
    close_system(modelName, 0);
end
end

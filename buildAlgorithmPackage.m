function manifestPath = buildAlgorithmPackage(modelName)
%BUILDALGORITHMPACKAGE Baut ein Simulink-Modell und erstellt ein Paket.
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
% Beispiel:
%   buildAlgorithmPackage('C_Notbremsung')

    modelName = string(modelName);

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
        mkdir(packageDirectory);
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

    %% Manifest erzeugen
    manifest = struct;
    manifest.schemaVersion = 1;
    manifest.modelName = modelName;
    manifest.inputs = convertInterfaces(inports);
    manifest.outputs = convertInterfaces(outports);

    manifest.library = struct;
    manifest.library.fileName = libraryFileName;
    manifest.library.platform = getCurrentPlatform();

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

    % Datei jetzt schließen.
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


function result = convertInterfaces(interfaces)
%CONVERTINTERFACES Wandelt Code-Descriptor-Interfaces in Strukturen um.

    template = struct( ...
        "name", "", ...
        "dataType", "", ...
        "cType", "", ...
        "dimensions", []);

    result = repmat( ...
        template, ...
        1, ...
        numel(interfaces));

    for index = 1:numel(interfaces)

        interfaceInfo = interfaces(index);
        typeInfo = interfaceInfo.Type;

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

        result(index).name = ...
            string(interfaceInfo.GraphicalName);

        result(index).dataType = dataType;
        result(index).cType = cType;
        result(index).dimensions = dimensions;
    end
end


function libraryPath = findGeneratedLibrary( ...
    modelName, ...
    buildDirectory)
%FINDGENERATEDLIBRARY Sucht die erzeugte native Bibliothek.

    buildDirectory = string(buildDirectory);
    parentDirectory = string(fileparts(buildDirectory));

    if ismac

        libraryNames = modelName + ".dylib";

    elseif ispc

        libraryNames = modelName + ".dll";

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
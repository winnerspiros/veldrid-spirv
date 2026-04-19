using McMaster.Extensions.CommandLineUtils;
using Newtonsoft.Json;
using Newtonsoft.Json.Converters;

namespace Veldrid.SPIRV;

public class Program
{
    public static void Main(string[] args)
    {
        CommandLineApplication.Execute<Program>(args);
    }

    [Option("--search-path", "The set of directories to search for shader source files.", CommandOptionType.MultipleValue)]
    public string[] SearchPaths { get; } = [];

    [Option("--output-path", "The directory where compiled files are placed.", CommandOptionType.SingleValue)]
    public string OutputPath { get; } = string.Empty;

    [Option("--set", "The path to the JSON file containing shader variant definitions to compile.", CommandOptionType.SingleValue)]
    public string SetDefinitionPath { get; } = string.Empty;

    public void OnExecute()
    {
        if (!Directory.Exists(OutputPath))
        {
            Directory.CreateDirectory(OutputPath);
        }

        ShaderVariantDescription[]? descs;
        JsonSerializer serializer = new() { Formatting = Formatting.Indented };
        serializer.Converters.Add(new StringEnumConverter());
        using (StreamReader sr = File.OpenText(SetDefinitionPath))
        using (JsonTextReader jtr = new(sr))
        {
            descs = serializer.Deserialize<ShaderVariantDescription[]>(jtr);
        }

        if (descs is null || descs.Length == 0)
        {
            Console.Error.WriteLine("No shader variant descriptions found in the set definition file.");
            return;
        }

        HashSet<string> generatedPaths = [];

        VariantCompiler compiler = new(new List<string>(SearchPaths), OutputPath);
        foreach (ShaderVariantDescription desc in descs)
        {
            string[] newPaths = compiler.Compile(desc);
            foreach (string s in newPaths)
            {
                generatedPaths.Add(s);
            }
        }

        string generatedFilesListText = string.Join(Environment.NewLine, generatedPaths);
        string generatedFilesListPath = Path.Combine(OutputPath, "vspv_generated_files.txt");
        File.WriteAllText(generatedFilesListPath, generatedFilesListText);
    }
}

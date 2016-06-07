// MatLabAdapter.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#pragma comment(lib, "libmx.lib")
#pragma comment(lib, "libmex.lib")
#pragma comment(lib, "libeng.lib")
#include "engine.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

void logMatlabCommand(const char* command)
{
	cout << "Matlab >> " << command << endl;
}

int main(int argc, char* argv[])
{
	try
	{
		if (argc <= 3)
		{
			cout << "Syntax: MatLabDemo <path to m file> <name of m file without .m> <output file> [<args file> [-d]]" << endl;
			return EXIT_FAILURE;
		}

		// loading Matlab engine
		cout << "Loading MatLab engine..." << endl << flush;
		Engine *engine;
		engine = engOpen("\0"); //(NULL);
		cout << "MatLab engine loaded" << endl << flush;
		if (engine == NULL)
		{
			cerr << "Error: Not Found" << endl;
			cerr << "Do you have Administrator privileges?" << endl;
			exit(EXIT_FAILURE);
		}

		// hiding the engine
		engSetVisible(engine, false);

		// preparing the engine...
		mxArray *waiter = NULL;
		int tryCount = 0;
		cout << "Trying to init ";
		do
		{
			if (tryCount == 0)
				cout << "*" << flush;
			else if (waiter != NULL)
				cout << "+" << flush;
			else
				cout << "-" << flush;
			mxArray *input = mxCreateScalarDouble(5);
			engPutVariable(engine, "X", input);
			waiter = engGetVariable(engine, "X");
			++tryCount;
		}
		while (waiter == NULL || tryCount < 3);
		cout << endl;

		// changing current directory to the location of the m file
		char *cdCommand = new char[strlen(argv[1]) + 8];
		strcpy(cdCommand, "cd('");
		strcat(cdCommand, argv[1]);
		strcat(cdCommand, "');");
		logMatlabCommand(cdCommand);
		engEvalString(engine, cdCommand);
		delete [] cdCommand;

		// determining number of return values
		int numberOfResults = 1;
		char mFilePath[_MAX_PATH];
		strcpy(mFilePath, argv[1]);
		char lastChar = mFilePath[strlen(mFilePath) - 1];
		if (lastChar != '\\' && lastChar != '/')
			strcat(mFilePath, "\\");
		strcat(mFilePath, argv[2]);
		strcat(mFilePath, ".m");
		cout << "m file path: " << mFilePath << endl;
		ifstream mFile(mFilePath, ios::in);
		if (!mFile)
		{
			cerr << "Error: cannot open file " << mFilePath << endl;
			engClose(engine);
			throw "";
		}
		else
		{
			char line[16384];
			while (mFile)
			{
				mFile.getline(line, 16384);
				// TODO what if the % sign is preceded by spaces?
				if (mFile && line[0] && line[0] != '%')
				{
					char *leftBracket = strchr(line, '[');
					char *equalsSign = strchr(line, '=');
					if (leftBracket != NULL && leftBracket < equalsSign)
					{
						numberOfResults = 0;
						char *end = strchr(leftBracket + 1, ']') - 1;
						char *p = leftBracket + 1;
						while (*p == ' ') ++p;
						while (*end == ' ') --end;
						++end; // sentinel
						bool inVar = false;
						while (p <= end)
						{
							if ((*p == ' ' || *p == ',' || *p == ']') && inVar)
							{
								++numberOfResults;
								inVar = false;
							}
							else if (*p != ' ' && *p != ',' && !inVar)
							{
								inVar = true;
							}
							++p;
						}
						if (numberOfResults > 0)
							break;
					}
				}
			}

			mFile.close();
		}
		cout << "Number of return values: " << numberOfResults << endl;
		
		// setting input variables - X0, X1, etc.
		int numberOfArgs = 0;
		char varName[12];
		strcpy(varName, "X");
		if (argc > 4)
		{
			ifstream argsFile(argv[4]);
			if (!argsFile)
			{
				cerr << "Error: cannot open file " << argv[4] << endl;
				engClose(engine);
				throw "";
			}
			char line[65536];
			while (argsFile)
			{
				argsFile.getline(line, 65536);
				if (argsFile)
				{
					mxArray *var;
					itoa(numberOfArgs, varName + 1, 10);
					string varInitCommand;
					varInitCommand += varName;
					varInitCommand += " = ";
					varInitCommand += line;
					engEvalString(engine, varInitCommand.data());
					logMatlabCommand(varInitCommand.data());
					++numberOfArgs;
				}
			}
			argsFile.close();
		}
		cout << "Number of arguments: " << numberOfArgs << endl;
		
		// constructing function call
		string functionCall;
		if (numberOfResults == 1)
		{
			functionCall = "Y0 = ";
		}
		else
		{
			varName[0] = 'Y';
			functionCall = "[Y0";
			for (int i = 1; i < numberOfResults; i++)
			{
				functionCall += ',';
				itoa(i, varName + 1, 10);
				functionCall += varName;
			}
			functionCall += "] = ";
		}
		functionCall += argv[2];
		functionCall += '(';
		if (numberOfArgs > 0)
		{
			varName[0] = 'X';
			functionCall += "X0";
			for (int i = 1; i < numberOfArgs; i++)
			{
				functionCall += ',';
				itoa(i, varName + 1, 10);
				functionCall += varName;
			}
		}
		functionCall += ");";
		logMatlabCommand(functionCall.data());

		// executing the m file
		const char* clearFunctionCacheCommand = "clear functions";
		engEvalString(engine, clearFunctionCacheCommand);
		engEvalString(engine, functionCall.data());
		engEvalString(engine, clearFunctionCacheCommand);

		// fetching the result
		char *outputPath = argv[3];
		ofstream output(outputPath);
		cout << "Output file: " << outputPath << endl;
		varName[0] = 'Y';
		for (int i = 0; i < numberOfResults; i++)
		{
			itoa(i, varName + 1, 10);
			mxArray *result = engGetVariable(engine, varName);
			if (result == NULL)
			{
				cerr << "Fatal error: Matlab engine returned null result\n";
				output.close();
				engClose(engine);
				throw "";
			}
			else
			{
				// do not use mxIsNumeric(result)

				char *outputLine;
				if (mxIsChar(result))
				{
					outputLine = mxArrayToString(result);
					output << "'" << outputLine << "'" << endl;
					cout << varName << " = '" << outputLine << "'" << endl;
				}
				else
				{
					// The mat2str function is intended to operate on scalar, vector,
					// or rectangular array inputs only.
					// An error will result if A is a multidimensional array.
					string mat2strCommand = varName;
					mat2strCommand += " = ";
					mat2strCommand += "mat2str(";
					mat2strCommand += varName;
					mat2strCommand += ");";
					logMatlabCommand(mat2strCommand.data());
					cout << "engEvalString returned " << engEvalString(engine, mat2strCommand.data()) << endl;

					mxArray* stringResult = engGetVariable(engine, varName);
					outputLine = mxArrayToString(stringResult);
					
					output //<< resultType << endl
						<< outputLine << endl;
					cout << /*resultType << ' ' <<*/ varName << " = " << outputLine << endl;
					mxDestroyArray(stringResult);
				}
				mxFree(outputLine);
				mxDestroyArray(result);
			}
		}
		output.close();

		engClose(engine);

		// for debugging only
		if (argc > 5 && !strncmp(argv[5], "-d", 2))
			system("pause");

		return EXIT_SUCCESS;

	}
	catch (const char*)
	{
		// for debugging only
		if (argc > 5 && !strncmp(argv[5], "-d", 2))
			system("pause");
		return EXIT_FAILURE;
	}
}

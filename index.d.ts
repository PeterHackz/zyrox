// noinspection JSUnusedGlobalSymbols

declare enum ObfuscationType {
    MixedBooleanArithmetic,
    ControlFlowFlattening,
    BasicBlockSplitter,
    IndirectBranch,
    SimpleIndirectBranch,
}

declare interface FunctionPassOptions {
    /**
     * @default 0
     * @description number of times a pass will run on a function
     */
    PassIterations?: number;

    "BasicBlockSplitter.SplitBlockMinSize"?: number;
    "BasicBlockSplitter.SplitBlockMaxSize"?: number;
    "BasicBlockSplitter.SplitBlockChance"?: number;

    "IndirectBranch.Chance"?: number;
    "SimpleIndirectBranch.Chance"?: number;

    "ControlFlowFlattening.UseFunctionResolverChance"?: number;
    "ControlFlowFlattening.UseGlobalStateVariablesChance"?: number;
    "ControlFlowFlattening.UseOpaqueTransformationChance"?: number;
    "ControlFlowFlattening.UseGlobalVariableOpaquesChance"?: number;
    "ControlFlowFlattening.UseSipHashedStateChance"?: number;
    "ControlFlowFlattening.CloneSipHashChance"?: number;
}

declare class z {

    static None: number;
    static Stack: number;
    static Global: number;

    static RegisterClass(claz: Object): void;

    static log(...args: any[]): void;

    static RegisterPass(PassType: ObfuscationType, PassOptions?: FunctionPassOptions): void;

    static AddMetaData(MetaData: string): void;

}

declare interface ZyroxPlugin {

    RunOnFunction(Name: string): void;

    OnString(Str: string): number;

    Init(): void;

}


#how to run 
#in R CLI
#source("Rscript-1.R") << this will load below lines
#then type "p" to draw the plot defined below

library(ggplot2)
library(grid)
R4_w1 <- read.csv("P-40Mbps-w-R1.txt", header = FALSE, '\t')
R4_w2 <- read.csv("P-40Mbps-w-R2.txt", header = FALSE, '\t')

p <- ggplot(data = R4_w1, aes(x = R4_w1$V1, y = R4_w1$V2)) + labs(color = "") + 
ggtitle("Deficit Round Robin") +
scale_colour_manual(breaks=c("Traffic 1", "Traffic 2"), labels=c("Traffic 1", "Traffic 2"), values=c("#FF0000", "#00bcbc")) + 
scale_x_continuous(limits= c(0, 80)) + scale_y_continuous(limits= c(13000, 16000)) + 
xlab("\nTime (s)\n") + ylab("\n bits/sec\n") + 
theme(
	plot.title = element_text(size = 28, colour ="black", face="bold", hjust = 0.5),
	axis.text = element_text(size=20, colour = "black"), 
	legend.title=element_blank(), legend.background = element_rect(colour = "black"), 
	legend.key = element_rect(fill = "white"), legend.key.size = unit(1.5, "cm"), 
	panel.grid.major = element_blank(), panel.grid.minor = element_blank(), 
	panel.background = element_blank(), axis.line = element_line(colour = "black"), 
	text = element_text(size=20)) + 
guides(colour = guide_legend(override.aes = list(size=3))) + 
expand_limits(x = 0)
p <- p + geom_line(data = R4_w1, aes(x = R4_w1$V1, y = R4_w1$V2, colour="Traffic 1"))
p <- p + geom_line(data = R4_w2, aes(x = R4_w2$V1, y = R4_w2$V2, colour="Traffic 2"))

